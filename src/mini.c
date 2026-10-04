/* Mini View: a small always-on-top window with the main readings */
#include "rt.h"
#include "app.h"
#include "resource.h"

#define ID_SILENT 2001
#define ID_NORMAL 2002
#define ID_FULL   2003
#define ID_LOG    2004
#define ID_TOP    2005
#define ID_BIG    2006

static HFONT g_mono, g_monoBold;
static int g_panelH;

void ui_draw_log_button(DRAWITEMSTRUCT *di, const char *label);

static int mini_rows(void)
{
    int i, n = 0;
    for (i = 0; i < NSENS; i++) if (i != S_AUX) n++;
    for (i = 0; i < NFAN; i++) n++;
    return n;
}

static void paint_panel(HDC out, RECT *pr)
{
    HDC dc = CreateCompatibleDC(out);
    int W = pr->right - pr->left, H = pr->bottom - pr->top, y = 6, i;
    HBITMAP bmp = CreateCompatibleBitmap(out, W, H), ob = (HBITMAP)SelectObject(dc, bmp);
    HFONT of = (HFONT)SelectObject(dc, g_mono);
    RECT r;
    char b[48], n[16];
    HPEN pen, op;
    SetRect(&r, 0, 0, W, H);
    FillRect(dc, &r, (HBRUSH)GetStockObject(BLACK_BRUSH));
    SetBkMode(dc, TRANSPARENT);
    for (i = 0; i < NSENS; i++) {
        Sensor *s = &g.s[i];
        int over = s->avail && s->val > s->limit;
        if (i == S_AUX) continue;
        SelectObject(dc, g_mono);
        SetTextColor(dc, s->avail ? s->color : RGB(128, 128, 128));
        SetTextAlign(dc, TA_LEFT | TA_TOP);
        TextOutA(dc, 8, y, s->shortName, rt_len(s->shortName));
        if (s->avail) app_ft(b, s->val); else rt_cpy(b, "n/a", sizeof(b));
        if (over) {
            RECT hr;
            HBRUSH br = CreateSolidBrush(RGB(192, 0, 0));
            SetRect(&hr, W - 90, y - 1, W - 4, y + 15);
            FillRect(dc, &hr, br);
            DeleteObject(br);
        }
        SelectObject(dc, g_monoBold);
        SetTextColor(dc, over ? RGB(255, 255, 255) : (s->avail ? s->color : RGB(128, 128, 128)));
        SetTextAlign(dc, TA_RIGHT | TA_TOP);
        TextOutA(dc, W - 8, y, b, rt_len(b));
        y += 16;
    }
    y += 4;
    pen = CreatePen(PS_SOLID, 1, RGB(96, 96, 96));
    op = (HPEN)SelectObject(dc, pen);
    MoveToEx(dc, 8, y, 0);
    LineTo(dc, W - 8, y);
    SelectObject(dc, op);
    DeleteObject(pen);
    y += 6;
    SelectObject(dc, g_mono);
    for (i = 0; i < NFAN; i++) {
        Fan *f = &g.f[i];
        SetTextColor(dc, f->avail ? RGB(208, 208, 208) : RGB(128, 128, 128));
        SetTextAlign(dc, TA_LEFT | TA_TOP);
        TextOutA(dc, 8, y, f->shortName, rt_len(f->shortName));
        if (!f->avail) rt_cpy(b, "n/a", sizeof(b));
        else if (f->ctrl && f->duty >= 0) wsprintfA(b, "%s rpm %3d%%", rt_fmtint(n, f->rpm), rt_round(f->duty));
        else wsprintfA(b, "%s rpm", rt_fmtint(n, f->rpm));
        SetTextAlign(dc, TA_RIGHT | TA_TOP);
        TextOutA(dc, W - 8, y, b, rt_len(b));
        y += 16;
    }
    SetTextAlign(dc, TA_LEFT | TA_TOP);
    BitBlt(out, pr->left, pr->top, W, H, dc, 0, 0, SRCCOPY);
    SelectObject(dc, of);
    SelectObject(dc, ob);
    DeleteObject(bmp);
    DeleteDC(dc);
}

static void panel_rect(HWND w, RECT *r)
{
    RECT c;
    GetClientRect(w, &c);
    SetRect(r, 6, 6, c.right - 6, 6 + g_panelH);
}

void mini_refresh(void)
{
    RECT r;
    int cur = app_cur_profile(), ctl = app_count_ctrl() > 0;
    if (!g.mini) return;
    panel_rect(g.mini, &r);
    InvalidateRect(g.mini, &r, FALSE);
    SendDlgItemMessageA(g.mini, ID_SILENT, BM_SETCHECK, cur == P_SILENT, 0);
    SendDlgItemMessageA(g.mini, ID_NORMAL, BM_SETCHECK, cur == P_NORMAL, 0);
    SendDlgItemMessageA(g.mini, ID_FULL, BM_SETCHECK, cur == P_FULL, 0);
    EnableWindow(GetDlgItem(g.mini, ID_SILENT), ctl);
    EnableWindow(GetDlgItem(g.mini, ID_NORMAL), ctl);
    EnableWindow(GetDlgItem(g.mini, ID_FULL), ctl);
    SendDlgItemMessageA(g.mini, ID_TOP, BM_SETCHECK, g.onTop, 0);
    InvalidateRect(GetDlgItem(g.mini, ID_LOG), 0, FALSE);
}

static HWND button(HWND p, const char *t, int id, DWORD style, int x, int y, int w, int h)
{
    HWND b = CreateWindowA("BUTTON", t, WS_CHILD | WS_VISIBLE | WS_TABSTOP | style, x, y, w, h, p, (HMENU)id, g.inst, 0);
    SendMessageA(b, WM_SETFONT, (WPARAM)g.font, 0);
    return b;
}

void mini_create(void)
{
    RECT r;
    int cw = 244, y;
    HWND lbl;
    if (g.mini) return;
    if (!g_mono) {
        g_mono = CreateFontA(-12, 0, 0, 0, FW_NORMAL, 0, 0, 0, ANSI_CHARSET, 0, 0, DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, "Courier New");
        g_monoBold = CreateFontA(-12, 0, 0, 0, FW_BOLD, 0, 0, 0, ANSI_CHARSET, 0, 0, DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, "Courier New");
    }
    g_panelH = 6 + mini_rows() * 16 + 10 + 6;
    y = 6 + g_panelH + 6;
    SetRect(&r, 0, 0, cw, y + 26 + 4 + 26 + 6);
    AdjustWindowRectEx(&r, WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE, 0);
    {
        RECT wa;
        SystemParametersInfoA(SPI_GETWORKAREA, 0, &wa, 0);
        g.mini = CreateWindowExA(g.onTop ? WS_EX_TOPMOST : 0, "neoFanSpeedMini", APP_NAME,
                                 WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                 wa.right - (r.right - r.left) - 12, wa.top + 12, r.right - r.left, r.bottom - r.top,
                                 0, 0, g.inst, 0);
    }
    lbl = CreateWindowA("STATIC", "Profile", WS_CHILD | WS_VISIBLE, 6, y + 6, 40, 16, g.mini, (HMENU)-1, g.inst, 0);
    SendMessageA(lbl, WM_SETFONT, (WPARAM)g.font, 0);
    button(g.mini, "Silent", ID_SILENT, BS_CHECKBOX | BS_PUSHLIKE, 48, y, 62, 24);
    button(g.mini, "Normal", ID_NORMAL, BS_CHECKBOX | BS_PUSHLIKE, 112, y, 62, 24);
    button(g.mini, "Full", ID_FULL, BS_CHECKBOX | BS_PUSHLIKE, 176, y, 62, 24);
    y += 30;
    button(g.mini, "", ID_LOG, BS_OWNERDRAW, 6, y, 70, 24);
    button(g.mini, "On top", ID_TOP, BS_AUTOCHECKBOX, 84, y + 4, 70, 16);
    button(g.mini, "Full View", ID_BIG, BS_PUSHBUTTON, 168, y, 70, 24);
    mini_refresh();
}

LRESULT CALLBACK mini_proc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(w, &ps);
        RECT r, e;
        panel_rect(w, &r);
        e = r;
        InflateRect(&e, 2, 2);
        DrawEdge(dc, &e, EDGE_SUNKEN, BF_RECT);
        paint_panel(dc, &r);
        EndPaint(w, &ps);
        return 0;
    }
    case WM_DRAWITEM:
        if (wp == ID_LOG) {
            ui_draw_log_button((DRAWITEMSTRUCT *)lp, g.log.on ? "Stop" : "Log");
            return TRUE;
        }
        break;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_SILENT: app_set_all(P_SILENT); break;
        case ID_NORMAL: app_set_all(P_NORMAL); break;
        case ID_FULL: app_set_all(P_FULL); break;
        case ID_LOG: log_toggle(); break;
        case ID_TOP:
            SendMessageA(g.main, WM_COMMAND, IDM_ONTOP, 0);
            break;
        case ID_BIG:
            g.miniView = 0;
            ShowWindow(w, SW_HIDE);
            ShowWindow(g.main, SW_SHOW);
            SetForegroundWindow(g.main);
            break;
        }
        ui_refresh();
        return 0;
    case WM_CLOSE:
        ShowWindow(w, SW_HIDE);
        return 0;
    case WM_DESTROY:
        g.mini = 0;
        return 0;
    }
    return DefWindowProcA(w, m, wp, lp);
}
