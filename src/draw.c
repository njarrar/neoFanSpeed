/* Custom drawn controls: temperature graph, fan curve, RPM display, banner */
#include "rt.h"
#include "app.h"
#include "resource.h"

#define YMIN 20
#define YMAX 80

static HBITMAP g_bmp;
static HDC g_mem;
static int g_bw, g_bh;

static HDC buffer_begin(HDC dc, int w, int h)
{
    if (!g_mem) g_mem = CreateCompatibleDC(dc);
    if (!g_bmp || w > g_bw || h > g_bh) {
        if (g_bmp) DeleteObject(g_bmp);
        g_bw = w > g_bw ? w : g_bw;
        g_bh = h > g_bh ? h : g_bh;
        g_bmp = CreateCompatibleBitmap(dc, g_bw, g_bh);
    }
    SelectObject(g_mem, g_bmp);
    return g_mem;
}

static void fill(HDC dc, int l, int t, int r, int b, COLORREF c)
{
    RECT rc;
    HBRUSH br = CreateSolidBrush(c);
    SetRect(&rc, l, t, r, b);
    FillRect(dc, &rc, br);
    DeleteObject(br);
}

static void line(HDC dc, int x1, int y1, int x2, int y2)
{
    MoveToEx(dc, x1, y1, 0);
    LineTo(dc, x2, y2);
}

static void text_at(HDC dc, int x, int y, const char *s, UINT align)
{
    SetTextAlign(dc, align);
    TextOutA(dc, x, y, s, rt_len(s));
}

/* ------------------------------------------------------------------ */
/* temperature history                                                 */

static int ymap(double c, int top, int h)
{
    double p = (c - YMIN) / (YMAX - YMIN);
    if (p < 0) p = 0;
    if (p > 1) p = 1;
    return top + h - (int)(p * h + 0.5);
}

static void paint_graph(HWND w, HDC out)
{
    RECT rc;
    HDC dc;
    int W, H, L = 34, R, T = 6, B, i, k, x, y;
    HPEN grid, old;
    char s[32];
    int ly[NSENS], li[NSENS], nl = 0;
    GetClientRect(w, &rc);
    W = rc.right; H = rc.bottom;
    dc = buffer_begin(out, W, H);
    R = W - 58; B = H - 16;
    fill(dc, 0, 0, W, H, GetSysColor(COLOR_BTNFACE));
    fill(dc, L, T, R, B, RGB(0, 0, 0));
    SelectObject(dc, g.font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));

    grid = CreatePen(PS_SOLID, 1, RGB(0, 110, 0));
    old = (HPEN)SelectObject(dc, grid);
    /* vertical lines every 10 s, sliding with time */
    for (i = 0; i <= 6; i++) {
        int shift = (g.ticks % 10) * (R - L) / 60;
        x = L + i * (R - L) / 6 - shift;
        if (x > L && x < R) line(dc, x, T, x, B);
    }
    for (k = YMIN; k <= YMAX; k += 20) {
        double c = k;
        int lab = k;
        if (g.unit == 'F') { lab = 80 + (k - YMIN) / 20 * 30; c = (lab - 32) / 1.8; }
        y = ymap(c, T, B - T);
        if (k != YMIN && k != YMAX) line(dc, L, y, R, y);
        wsprintfA(s, "%d\xB0", lab);
        text_at(dc, L - 4, y - 7, s, TA_RIGHT | TA_TOP);
    }
    SelectObject(dc, old);
    DeleteObject(grid);

    /* alarm limit of the selected sensor */
    if (g.s[g.selSensor].avail) {
        HPEN p = CreatePen(PS_DOT, 1, RGB(128, 128, 64));
        old = (HPEN)SelectObject(dc, p);
        SetBkMode(dc, TRANSPARENT);
        y = ymap(g.s[g.selSensor].limit, T, B - T);
        line(dc, L, y, R, y);
        SelectObject(dc, old);
        DeleteObject(p);
    }

    for (i = 0; i < NSENS; i++) {
        Sensor *se = &g.s[i];
        HPEN p;
        int n = se->nhist;
        if (!se->avail || !se->plot || n < 2) continue;
        p = CreatePen(se->dash, 1, se->color);
        old = (HPEN)SelectObject(dc, p);
        SetBkMode(dc, TRANSPARENT);
        for (k = 0; k < n; k++) {
            x = R - 1 - (n - 1 - k) * (R - L - 1) / (HIST - 1);
            y = ymap(se->hist[k], T, B - T);
            if (k == 0) MoveToEx(dc, x, y, 0);
            else LineTo(dc, x, y);
        }
        SelectObject(dc, old);
        DeleteObject(p);
        li[nl] = i;
        ly[nl++] = ymap(se->val, T, B - T);
    }

    /* labels to the right, kept apart */
    for (i = 0; i < nl; i++)
        for (k = i + 1; k < nl; k++)
            if (ly[k] < ly[i]) { int t = ly[i]; ly[i] = ly[k]; ly[k] = t; t = li[i]; li[i] = li[k]; li[k] = t; }
    for (i = 1; i < nl; i++)
        if (ly[i] < ly[i - 1] + 14) ly[i] = ly[i - 1] + 14;
    for (i = 0; i < nl; i++) {
        y = ly[i];
        if (y > B) y = B;
        fill(dc, R + 8, y - 5, R + 18, y + 5, RGB(0, 0, 0));
        fill(dc, R + 9, y - 4, R + 17, y + 4, g.s[li[i]].color);
        SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
        text_at(dc, R + 22, y - 7, g.s[li[i]].shortName, TA_LEFT | TA_TOP);
    }
    text_at(dc, L, B + 2, "-60 s", TA_LEFT | TA_TOP);
    text_at(dc, (L + R) / 2, B + 2, "-30 s", TA_CENTER | TA_TOP);
    text_at(dc, R, B + 2, "now", TA_RIGHT | TA_TOP);
    SetTextAlign(dc, TA_LEFT | TA_TOP);
    BitBlt(out, 0, 0, W, H, dc, 0, 0, SRCCOPY);
}

/* ------------------------------------------------------------------ */
/* fan curve                                                           */

static void paint_curve(HWND w, HDC out)
{
    RECT rc;
    HDC dc;
    int W, H, i, x, y;
    HPEN p, old;
    Fan *f = &g.f[g.selFan];
    POINT pt[NPTS + 2];
    double t;
    GetClientRect(w, &rc);
    W = rc.right; H = rc.bottom;
    dc = buffer_begin(out, W, H);
    fill(dc, 0, 0, W, H, GetSysColor(COLOR_WINDOW));
    p = CreatePen(PS_SOLID, 1, RGB(224, 224, 224));
    old = (HPEN)SelectObject(dc, p);
    for (i = 1; i < 4; i++) line(dc, 0, i * H / 4, W, i * H / 4);
    SelectObject(dc, old);
    DeleteObject(p);

    p = CreatePen(PS_DOT, 1, RGB(220, 0, 0));
    old = (HPEN)SelectObject(dc, p);
    SetBkMode(dc, TRANSPARENT);
    y = H - 1 - g.o.floor * (H - 2) / 100;
    line(dc, 0, y, W, y);
    SelectObject(dc, old);
    DeleteObject(p);

    pt[0].x = 0;
    pt[0].y = H - 1 - f->pts[0][1] * (H - 2) / 100;
    for (i = 0; i < NPTS; i++) {
        int tx = f->pts[i][0];
        if (tx < YMIN) tx = YMIN;
        if (tx > YMAX) tx = YMAX;
        pt[i + 1].x = (tx - YMIN) * (W - 1) / (YMAX - YMIN);
        pt[i + 1].y = H - 1 - f->pts[i][1] * (H - 2) / 100;
    }
    pt[NPTS + 1].x = W - 1;
    pt[NPTS + 1].y = pt[NPTS].y;
    p = CreatePen(PS_SOLID, 2, RGB(0, 0, 128));
    old = (HPEN)SelectObject(dc, p);
    Polyline(dc, pt, NPTS + 2);
    SelectObject(dc, old);
    DeleteObject(p);

    t = app_temp_of(f->follows);
    if (t > YMIN && t < YMAX) {
        p = CreatePen(PS_DASH, 1, RGB(0, 0, 0));
        old = (HPEN)SelectObject(dc, p);
        x = (int)((t - YMIN) * (W - 1) / (YMAX - YMIN));
        line(dc, x, 0, x, H);
        SelectObject(dc, old);
        DeleteObject(p);
    }
    p = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_BTNSHADOW));
    old = (HPEN)SelectObject(dc, p);
    SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, 0, 0, W, H);
    SelectObject(dc, old);
    DeleteObject(p);
    BitBlt(out, 0, 0, W, H, dc, 0, 0, SRCCOPY);
}

/* ------------------------------------------------------------------ */
/* RPM display                                                         */

static void paint_led(HWND w, HDC out)
{
    RECT rc;
    HDC dc;
    int W, H;
    char s[40];
    SIZE sz;
    GetWindowTextA(w, s, sizeof(s));
    GetClientRect(w, &rc);
    W = rc.right; H = rc.bottom;
    dc = buffer_begin(out, W, H);
    fill(dc, 0, 0, W, H, GetSysColor(COLOR_BTNFACE));
    DrawEdge(dc, &rc, EDGE_SUNKEN, BF_RECT);
    fill(dc, 2, 2, W - 2, H - 2, RGB(0, 0, 0));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(0, 255, 0));
    SelectObject(dc, g.smallFont);
    GetTextExtentPoint32A(dc, "RPM", 3, &sz);
    text_at(dc, W - 8 - sz.cx, H - 8 - sz.cy, "RPM", TA_LEFT | TA_TOP);
    SelectObject(dc, g.ledFont);
    SetTextAlign(dc, TA_RIGHT | TA_BASELINE);
    TextOutA(dc, W - 14 - sz.cx, H - 8, s, rt_len(s));
    SetTextAlign(dc, TA_LEFT | TA_TOP);
    BitBlt(out, 0, 0, W, H, dc, 0, 0, SRCCOPY);
}

/* ------------------------------------------------------------------ */
/* banner: window text is "title\nbody"                                */

static void paint_banner(HWND w, HDC dc)
{
    RECT rc, tr;
    char s[400], *body;
    int i;
    HBRUSH br;
    GetClientRect(w, &rc);
    br = CreateSolidBrush(GetSysColor(COLOR_INFOBK));
    FillRect(dc, &rc, br);
    DeleteObject(br);
    FrameRect(dc, &rc, (HBRUSH)GetStockObject(GRAY_BRUSH));
    DrawIcon(dc, 10, (rc.bottom - 32) / 2, LoadIcon(0, IDI_EXCLAMATION));
    GetWindowTextA(w, s, sizeof(s));
    body = s;
    for (i = 0; s[i]; i++) if (s[i] == '\n') { s[i] = 0; body = s + i + 1; break; }
    if (body == s) body = s + rt_len(s);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, GetSysColor(COLOR_INFOTEXT));
    SelectObject(dc, g.bold);
    SetRect(&tr, 52, 6, rc.right - 170, 20);
    DrawTextA(dc, s, -1, &tr, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    SelectObject(dc, g.font);
    SetRect(&tr, 52, 22, rc.right - 170, rc.bottom - 3);
    DrawTextA(dc, body, -1, &tr, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
}

static LRESULT CALLBACK ctl_proc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(w, &ps);
        char cls[16];
        GetClassNameA(w, cls, sizeof(cls));
        if (rt_ieq(cls, "NFSGraph")) paint_graph(w, dc);
        else if (rt_ieq(cls, "NFSCurve")) paint_curve(w, dc);
        else if (rt_ieq(cls, "NFSLed")) paint_led(w, dc);
        else paint_banner(w, dc);
        EndPaint(w, &ps);
        return 0;
    }
    case WM_SETTEXT: {
        LRESULT r = DefWindowProcA(w, m, wp, lp);
        InvalidateRect(w, 0, FALSE);
        return r;
    }
    case WM_COMMAND:
    case WM_CTLCOLORBTN:
        return SendMessageA(GetParent(w), m, wp, lp);
    }
    return DefWindowProcA(w, m, wp, lp);
}

void draw_register(HINSTANCE inst)
{
    WNDCLASSA wc;
    static const char *names[] = { "NFSGraph", "NFSCurve", "NFSLed", "NFSBanner" };
    int i;
    for (i = 0; i < 4; i++) {
        ZERO(wc);
        wc.lpfnWndProc = ctl_proc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursor(0, IDC_ARROW);
        wc.lpszClassName = names[i];
        wc.style = CS_HREDRAW | CS_VREDRAW;
        RegisterClassA(&wc);
    }
}
