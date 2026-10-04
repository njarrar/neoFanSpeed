/* Main window, tab pages, tray icon, alarm and detection dialogs */
#include "rt.h"
#include "app.h"
#include "resource.h"
#include <commdlg.h>
#include <shellapi.h>

#define WM_TRAY   (WM_APP + 1)
#define TIMER_ID  1
#define TRAY_ID   1

static const int g_intervals[] = { 1, 2, 5, 10, 30, 60 };
static int g_pageW, g_pageH, g_tabExtraW, g_tabExtraH;
static HICON g_trayIcon;
static int g_bannerShown;

/* ------------------------------------------------------------------ */
/* small helpers                                                       */

static HWND pg(int page, int id) { return GetDlgItem(g.page[page], id); }

static void set_text(HWND w, const char *s)
{
    char old[512];
    if (!w) return;
    GetWindowTextA(w, old, sizeof(old));
    if (!rt_ieq(old, s) || rt_len(old) != rt_len(s)) SetWindowTextA(w, s);
}

static void show(HWND w, int on)
{
    if (w && (IsWindowVisible(w) ? 1 : 0) != (on ? 1 : 0)) ShowWindow(w, on ? SW_SHOW : SW_HIDE);
}

static void enable(HWND w, int on)
{
    if (w && (IsWindowEnabled(w) ? 1 : 0) != (on ? 1 : 0)) EnableWindow(w, on);
}

static void check(HWND w, int on)
{
    if (w && (SendMessageA(w, BM_GETCHECK, 0, 0) == BST_CHECKED) != (on ? 1 : 0))
        SendMessageA(w, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
}

static int checked(HWND w) { return SendMessageA(w, BM_GETCHECK, 0, 0) == BST_CHECKED; }

static void spin_init(HWND w)
{
    if (!w) return;
    SendMessageA(w, UDM_SETRANGE, 0, MAKELONG(1000, 0));
    SendMessageA(w, UDM_SETPOS, 0, MAKELONG(500, 0));
}

static void lv_text(HWND lv, int item, int sub, const char *s)
{
    char old[128];
    LVITEMA it;
    ZERO(it);
    it.iSubItem = sub;
    it.pszText = old;
    it.cchTextMax = sizeof(old);
    old[0] = 0;
    SendMessageA(lv, LVM_GETITEMTEXTA, item, (LPARAM)&it);
    if (rt_len(old) == rt_len(s) && rt_ieq(old, s)) return;
    it.pszText = (char *)s;
    SendMessageA(lv, LVM_SETITEMTEXTA, item, (LPARAM)&it);
}

static void lv_col(HWND lv, int i, const char *name, int w, int right)
{
    LVCOLUMNA c;
    ZERO(c);
    c.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT | LVCF_SUBITEM;
    c.fmt = right ? LVCFMT_RIGHT : LVCFMT_LEFT;
    c.cx = w;
    c.pszText = (char *)name;
    c.iSubItem = i;
    SendMessageA(lv, LVM_INSERTCOLUMNA, i, (LPARAM)&c);
}

static void lv_add(HWND lv, int i, const char *s)
{
    LVITEMA it;
    ZERO(it);
    it.mask = LVIF_TEXT;
    it.iItem = i;
    it.pszText = (char *)s;
    SendMessageA(lv, LVM_INSERTITEMA, 0, (LPARAM)&it);
}

static int lv_checked(HWND lv, int i)
{
    return ((SendMessageA(lv, LVM_GETITEMSTATE, i, LVIS_STATEIMAGEMASK) >> 12) - 1) == 1;
}

static void lv_check(HWND lv, int i, int on)
{
    LVITEMA it;
    if (lv_checked(lv, i) == (on ? 1 : 0)) return;
    ZERO(it);
    it.stateMask = LVIS_STATEIMAGEMASK;
    it.state = INDEXTOSTATEIMAGEMASK(on ? 2 : 1);
    SendMessageA(lv, LVM_SETITEMSTATE, i, (LPARAM)&it);
}

static void lv_select(HWND lv, int i)
{
    LVITEMA it;
    ZERO(it);
    it.stateMask = LVIS_SELECTED | LVIS_FOCUSED;
    it.state = LVIS_SELECTED | LVIS_FOCUSED;
    SendMessageA(lv, LVM_SETITEMSTATE, i, (LPARAM)&it);
}

static void lv_image(HWND lv, int i, int sub, int img)
{
    LVITEMA it;
    ZERO(it);
    it.mask = LVIF_IMAGE;
    it.iItem = i;
    it.iSubItem = sub;
    it.iImage = img;
    SendMessageA(lv, LVM_SETITEMA, 0, (LPARAM)&it);
}

static const char *profile_label(int p)
{
    static const char *n[] = { "Silent", "Normal", "Full speed" };
    return (p >= 0 && p < 3) ? n[p] : "";
}

static void mode_name(Fan *f, char *out)
{
    switch (f->mode) {
    case M_AUTO: rt_cpy(out, "Auto (BIOS)", 32); break;
    case M_MANUAL: rt_cpy(out, "Manual", 32); break;
    case M_PROFILE: wsprintfA(out, "Profile: %s", profile_label(f->profile)); break;
    default: rt_cpy(out, "Curve", 32); break;
    }
}

static int fan_selectable_reason(Fan *f, char *out, int cap)
{
    int i = (int)(f - g.f);
    if (g.hwState == HWS_NT) {
        rt_cpy(out, "Windows 2000 and XP block programs from talking to the sensor chip directly, so fans cannot be read or set here.", cap);
        return 1;
    }
    if (g.hwState == HWS_NOCHIP) {
        rt_cpy(out, "No sensor chip was found, so this fan cannot be read or controlled.", cap);
        return 1;
    }
    if (g.hwState == HWS_UNSUPPORTED) {
        wsprintfA(out, "The sensor chip on this motherboard (%s) is not supported yet.", g.hw.chip.name);
        return 1;
    }
    if (!g.hw.chip.hasFan[i]) {
        rt_cpy(out, "No speed signal on this fan input. Either nothing is plugged in or the fan has no speed wire.", cap);
        return 1;
    }
    if (!g.hw.chip.canControl) {
        wsprintfA(out, "Speed control on the %s is not supported yet. Its speed can still be read and logged.", g.hw.chip.name);
        return 1;
    }
    if (!g.hw.chip.fanCtl[i]) {
        rt_cpy(out, "The sensor chip has no speed output for this fan input, so it runs at the speed the board gives it. Its speed can still be read and logged.", cap);
        return 1;
    }
    out[0] = 0;
    return 0;
}

/* ------------------------------------------------------------------ */
/* swatch images for the sensor list                                   */

static HIMAGELIST make_swatches(void)
{
    HIMAGELIST il = ImageList_Create(16, 16, ILC_COLOR8 | ILC_MASK, NSENS + 1, 0);
    HDC sdc = GetDC(0), dc = CreateCompatibleDC(sdc);
    int i;
    for (i = 0; i <= NSENS; i++) {
        HBITMAP b = CreateCompatibleBitmap(sdc, 16, 16), ob;
        RECT r;
        HBRUSH br;
        ob = (HBITMAP)SelectObject(dc, b);
        SetRect(&r, 0, 0, 16, 16);
        br = CreateSolidBrush(RGB(255, 0, 255));
        FillRect(dc, &r, br);
        DeleteObject(br);
        SetRect(&r, 3, 3, 13, 13);
        FillRect(dc, &r, (HBRUSH)GetStockObject(BLACK_BRUSH));
        SetRect(&r, 4, 4, 12, 12);
        br = CreateSolidBrush(i < NSENS ? g.s[i].color : RGB(192, 192, 192));
        FillRect(dc, &r, br);
        DeleteObject(br);
        SelectObject(dc, ob);
        ImageList_AddMasked(il, b, RGB(255, 0, 255));
        DeleteObject(b);
    }
    DeleteDC(dc);
    ReleaseDC(0, sdc);
    return il;
}

/* ------------------------------------------------------------------ */
/* tray icon                                                           */

typedef struct {
    DWORD cbSize;
    HWND hWnd;
    UINT uID, uFlags, uCallbackMessage;
    HICON hIcon;
    CHAR szTip[128];
    DWORD dwState, dwStateMask;
    CHAR szInfo[256];
    UINT uTimeout;
    CHAR szInfoTitle[64];
    DWORD dwInfoFlags;
} NFS_NID;
#define NID_OLD_SIZE 88
#define NFS_NIF_INFO 0x10
#define NFS_NIIF_WARNING 2

static int tray_source(void)
{
    if (g.s[S_CPU].avail) return S_CPU;
    if (g.s[S_BOARD].avail) return S_BOARD;
    if (g.s[S_GPU].avail) return S_GPU;
    if (g.s[S_HDD].avail) return S_HDD;
    return -1;
}

static HICON make_tray_icon(void)
{
    HDC sdc = GetDC(0), dc = CreateCompatibleDC(sdc);
    HBITMAP col = CreateCompatibleBitmap(sdc, 16, 16), mask = CreateBitmap(16, 16, 1, 1, 0), ob;
    HFONT f = CreateFontA(-10, 0, 0, 0, FW_BOLD, 0, 0, 0, ANSI_CHARSET, 0, 0, NONANTIALIASED_QUALITY, 0, "Arial"), of;
    ICONINFO ii;
    HICON ic;
    RECT r;
    HBRUSH br;
    char s[8];
    int src = tray_source();
    COLORREF bg = RGB(128, 128, 128);
    if (src >= 0) {
        double t = g.s[src].val, lim = g.s[src].limit;
        bg = t < lim - 10 ? RGB(0, 112, 0) : t < lim ? RGB(176, 112, 0) : RGB(192, 0, 0);
        wsprintfA(s, "%d", rt_round(app_cv(t)));
    } else {
        rt_cpy(s, "--", sizeof(s));
    }
    ob = (HBITMAP)SelectObject(dc, mask);
    SetRect(&r, 0, 0, 16, 16);
    FillRect(dc, &r, (HBRUSH)GetStockObject(BLACK_BRUSH));
    SelectObject(dc, col);
    FillRect(dc, &r, (HBRUSH)GetStockObject(BLACK_BRUSH));
    SetRect(&r, 1, 1, 15, 15);
    br = CreateSolidBrush(bg);
    FillRect(dc, &r, br);
    DeleteObject(br);
    of = (HFONT)SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    DrawTextA(dc, s, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, of);
    SelectObject(dc, ob);
    DeleteObject(f);
    ZERO(ii);
    ii.fIcon = TRUE;
    ii.hbmMask = mask;
    ii.hbmColor = col;
    ic = CreateIconIndirect(&ii);
    DeleteObject(col);
    DeleteObject(mask);
    DeleteDC(dc);
    ReleaseDC(0, sdc);
    return ic;
}

static void tray_tip(char *out, int cap)
{
    char a[16], b[64];
    int i;
    out[0] = 0;
    for (i = 0; i < NSENS; i++) {
        if (!g.s[i].avail || i == S_AUX) continue;
        wsprintfA(b, "%s%s %d%c", out[0] ? "  " : "", g.s[i].shortName, rt_round(app_cv(g.s[i].val)), g.unit);
        rt_cat(out, b, cap);
    }
    for (i = 0; i < NFAN; i++) {
        if (!g.f[i].avail) continue;
        wsprintfA(b, "%sFan %s rpm", out[0] ? "  " : "", rt_fmtint(a, g.f[i].rpm));
        rt_cat(out, b, cap);
        break;
    }
    if (!out[0]) rt_cpy(out, APP_NAME, cap);
}

static void tray_send(DWORD msg, const char *title, const char *info)
{
    NFS_NID n;
    HICON old = g_trayIcon;
    ZERO(n);
    n.cbSize = g.hw.os.hasBalloon ? sizeof(n) : NID_OLD_SIZE;
    n.hWnd = g.main;
    n.uID = TRAY_ID;
    n.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    n.uCallbackMessage = WM_TRAY;
    g_trayIcon = make_tray_icon();
    n.hIcon = g_trayIcon;
    tray_tip(n.szTip, g.hw.os.hasBalloon ? 128 : 64);
    if (title && g.hw.os.hasBalloon) {
        n.uFlags |= NFS_NIF_INFO;
        rt_cpy(n.szInfoTitle, title, sizeof(n.szInfoTitle));
        rt_cpy(n.szInfo, info, sizeof(n.szInfo));
        n.uTimeout = 10000;
        n.dwInfoFlags = NFS_NIIF_WARNING;
    }
    if (Shell_NotifyIconA(msg, (NOTIFYICONDATAA *)&n)) {
        if (msg == NIM_ADD) g.trayAdded = 1;
    } else if (msg == NIM_MODIFY) {
        if (Shell_NotifyIconA(NIM_ADD, (NOTIFYICONDATAA *)&n)) g.trayAdded = 1;
    }
    if (old) DestroyIcon(old);
}

void ui_tray_update(void)
{
    tray_send(g.trayAdded ? NIM_MODIFY : NIM_ADD, 0, 0);
}

void ui_balloon(const char *title, const char *text)
{
    tray_send(g.trayAdded ? NIM_MODIFY : NIM_ADD, title, text);
}

static void tray_remove(void)
{
    NFS_NID n;
    if (!g.trayAdded) return;
    ZERO(n);
    n.cbSize = NID_OLD_SIZE;
    n.hWnd = g.main;
    n.uID = TRAY_ID;
    Shell_NotifyIconA(NIM_DELETE, (NOTIFYICONDATAA *)&n);
    g.trayAdded = 0;
}

static void tray_menu(void)
{
    HMENU m = CreatePopupMenu();
    POINT p;
    int cur = app_cur_profile(), ctl = app_count_ctrl() > 0;
    AppendMenuA(m, MF_STRING, IDM_SHOW, "&Show neoFanSpeed");
    SetMenuDefaultItem(m, IDM_SHOW, FALSE);
    AppendMenuA(m, MF_SEPARATOR, 0, 0);
    AppendMenuA(m, MF_STRING | (cur == P_SILENT ? MF_CHECKED : 0) | (ctl ? 0 : MF_GRAYED), IDM_PSILENT, "S&ilent profile");
    AppendMenuA(m, MF_STRING | (cur == P_NORMAL ? MF_CHECKED : 0) | (ctl ? 0 : MF_GRAYED), IDM_PNORMAL, "&Normal profile");
    AppendMenuA(m, MF_STRING | (cur == P_FULL ? MF_CHECKED : 0) | (ctl ? 0 : MF_GRAYED), IDM_PFULL, "&Full speed profile");
    AppendMenuA(m, MF_SEPARATOR, 0, 0);
    AppendMenuA(m, MF_STRING, IDM_LOGTOGGLE, g.log.on ? "S&top Logging" : "S&tart Logging");
    AppendMenuA(m, MF_SEPARATOR, 0, 0);
    AppendMenuA(m, MF_STRING, IDM_EXIT, "E&xit");
    GetCursorPos(&p);
    SetForegroundWindow(g.main);
    TrackPopupMenu(m, TPM_RIGHTBUTTON, p.x, p.y, 0, g.main, 0);
    PostMessageA(g.main, WM_NULL, 0, 0);
    DestroyMenu(m);
}

/* ------------------------------------------------------------------ */
/* banner across the top                                               */

void ui_banner(void)
{
    char t[MAX_PATH + 400];
    const char *btn = 0;
    t[0] = 0;
    if (g.fsActive && !g.alarmDlg) {
        wsprintfA(t, "Fans set to full speed.\n%s %s %s at %s. Your own fan settings are saved.",
                  g.fsName, g.fsIsFan ? "dropped below" : "passed", g.fsLim, g.fsTime);
        btn = "Restore My Settings";
    } else if (g.hwState == HWS_NT) {
        wsprintfA(t, "Motherboard sensors are off on Windows %s.\nWindows %s blocks programs from the sensor chip, so board temperatures and fan control are off. %s",
                  g.hw.os.shortName, g.hw.os.shortName,
                  g.hw.os.isAdmin ? "CPU, graphics and hard disk details still work." : "Run as administrator to read the hard disk temperature.");
        btn = g.hw.os.isAdmin ? 0 : "Run as Administrator";
    } else if (g.hwState == HWS_NOCHIP) {
        rt_cpy(t, "No sensor chip found.\nMotherboard temperatures and fan control are unavailable. Hard disk readings still work.", sizeof(t));
        btn = "Scan Again";
    } else if (g.hwState == HWS_UNSUPPORTED) {
        wsprintfA(t, "Sensor chip not supported: %s.\nMotherboard temperatures and fan control stay off until this chip is supported.", g.hw.chip.name);
        if (g.reportMsg[0] && rt_starts(g.reportMsg, "Chip report"))
            wsprintfA(t, "Sensor chip not supported: %s.\n%s", g.hw.chip.name, g.reportMsg);
        btn = "Save Chip Report";
    }
    if (!t[0]) {
        if (g_bannerShown) { g_bannerShown = 0; ShowWindow(g.banner, SW_HIDE); ui_layout(); }
        return;
    }
    set_text(g.banner, t);
    if (btn) set_text(g.bannerBtn, btn);
    show(g.bannerBtn, btn != 0);
    if (!g_bannerShown) { g_bannerShown = 1; ShowWindow(g.banner, SW_SHOW); ui_layout(); }
    InvalidateRect(g.banner, 0, TRUE);
}

static void save_chip_report(void)
{
    char path[MAX_PATH], buf[4096];
    HANDLE h;
    DWORD w;
    rt_cpy(path, "C:\\NEOFAN\\CHIPRPT.TXT", sizeof(path));
    CreateDirectoryA("C:\\NEOFAN", 0);
    ui_build_report(buf, sizeof(buf));
    h = CreateFileA(path, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (h == INVALID_HANDLE_VALUE) {
        wsprintfA(path, "%s\\CHIPRPT.TXT", g.exeDir);
        h = CreateFileA(path, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    }
    if (h == INVALID_HANDLE_VALUE) {
        rt_cpy(g.reportMsg, "Chip report could not be saved.", sizeof(g.reportMsg));
        return;
    }
    WriteFile(h, buf, (DWORD)rt_len(buf), &w, 0);
    CloseHandle(h);
    wsprintfA(g.reportMsg, "Chip report saved to %s. Send it to the author to ask for support.", path);
}

static void run_as_admin(void)
{
    char exe[MAX_PATH];
    GetModuleFileNameA(0, exe, sizeof(exe));
    if ((INT_PTR)ShellExecuteA(g.main, "runas", exe, 0, 0, SW_SHOWNORMAL) > 32) {
        g.exiting = 1;
        PostMessageA(g.main, WM_COMMAND, IDM_EXIT, 0);
    } else {
        MessageBoxA(g.main, "Close neoFanSpeed, then hold Shift, right-click NEOFAN.EXE and pick \"Run as...\" to start it as an administrator.",
                    APP_NAME, MB_OK | MB_ICONINFORMATION);
    }
}

/* ------------------------------------------------------------------ */
/* layout                                                              */

void ui_layout(void)
{
    RECT rc, sr;
    int top = 36, statusH, tabW, tabH, cw, ch;
    GetWindowRect(g.status, &sr);
    statusH = sr.bottom - sr.top;
    tabW = g_pageW + g_tabExtraW;
    tabH = g_pageH + g_tabExtraH;
    cw = tabW + 12;
    if (g_bannerShown) {
        MoveWindow(g.banner, 6, top, cw - 12, 58, TRUE);
        MoveWindow(g.bannerBtn, cw - 12 - 156, 16, 146, 25, TRUE);
        top += 64;
    }
    MoveWindow(g.tab, 6, top, tabW, tabH, TRUE);
    SetRect(&rc, 6, top, 6 + tabW, top + tabH);
    TabCtrl_AdjustRect(g.tab, FALSE, &rc);
    {
        int i;
        for (i = 0; i < 4; i++) SetWindowPos(g.page[i], HWND_TOP, rc.left, rc.top, g_pageW, g_pageH, SWP_NOACTIVATE);
    }
    ch = top + tabH + 6 + statusH;
    SetRect(&rc, 0, 0, cw, ch);
    AdjustWindowRectEx(&rc, GetWindowLongA(g.main, GWL_STYLE), TRUE, GetWindowLongA(g.main, GWL_EXSTYLE));
    SetWindowPos(g.main, 0, 0, 0, rc.right - rc.left, rc.bottom - rc.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    SendMessageA(g.status, WM_SIZE, 0, 0);
    {
        int parts[3];
        parts[0] = cw * 50 / 100;
        parts[1] = cw * 83 / 100;
        parts[2] = -1;
        SendMessageA(g.status, SB_SETPARTS, 3, (LPARAM)parts);
    }
    InvalidateRect(g.main, 0, TRUE);
}

void ui_set_tab(int t)
{
    int i;
    if (t < 0 || t > 3) return;
    g.curTab = t;
    if (TabCtrl_GetCurSel(g.tab) != t) TabCtrl_SetCurSel(g.tab, t);
    for (i = 0; i < 4; i++) ShowWindow(g.page[i], i == t ? SW_SHOW : SW_HIDE);
    ui_refresh();
}

/* ------------------------------------------------------------------ */
/* status bar                                                          */

void ui_status(void)
{
    char a[MAX_PATH + 80], b[120], c[64], t[24], kb[24];
    int i, warn = 0, src = tray_source();
    const char *file = g.log.path, *p;
    for (p = g.log.path; *p; p++) if (*p == '\\') file = p + 1;
    if (g.log.error[0]) rt_cpy(a, g.log.error, sizeof(a));
    else if (g.log.on) wsprintfA(a, "Logging to %s (%s KB)", file, rt_fmtint(kb, (long)((g.log.bytes + 1023) / 1024)));
    else rt_cpy(a, "Not logging", sizeof(a));
    b[0] = 0;
    for (i = 0; i < NSENS && !b[0]; i++)
        if (g.s[i].avail && g.s[i].val > g.s[i].limit) { wsprintfA(b, "%s over limit", g.s[i].name); warn = 1; }
    for (i = 0; i < NFAN && !b[0]; i++)
        if (g.f[i].avail && g.f[i].alarmOn && g.f[i].rpm < g.f[i].alarm) { wsprintfA(b, "%s below %d RPM", g.f[i].name, g.f[i].alarm); warn = 1; }
    if (!b[0]) {
        if (g.hwState == HWS_NT) wsprintfA(b, "Limited: Windows %s", g.hw.os.shortName);
        else if (g.hwState == HWS_NOCHIP) rt_cpy(b, "Limited: no sensor chip", sizeof(b));
        else if (g.hwState == HWS_UNSUPPORTED) rt_cpy(b, "Limited: chip not supported", sizeof(b));
        else if (g.fsActive) rt_cpy(b, "Fans at full speed (fail-safe)", sizeof(b));
        else rt_cpy(b, "All readings normal", sizeof(b));
    }
    (void)warn;
    if (src >= 0) wsprintfA(c, "%s %s", g.s[src].shortName, app_ft(t, g.s[src].val));
    else rt_cpy(c, "No temperature", sizeof(c));
    {
        char old[MAX_PATH + 80];
        SendMessageA(g.status, SB_GETTEXTA, 0, (LPARAM)old);
        if (!rt_ieq(old, a)) SendMessageA(g.status, SB_SETTEXTA, 0, (LPARAM)a);
        SendMessageA(g.status, SB_GETTEXTA, 1, (LPARAM)old);
        if (!rt_ieq(old, b)) SendMessageA(g.status, SB_SETTEXTA, 1, (LPARAM)b);
        SendMessageA(g.status, SB_GETTEXTA, 2, (LPARAM)old);
        if (!rt_ieq(old, c)) SendMessageA(g.status, SB_SETTEXTA, 2, (LPARAM)c);
    }
}

/* ------------------------------------------------------------------ */
/* sensors page                                                        */

static void sensors_init(HWND d)
{
    HWND lv = GetDlgItem(d, IDC_SLIST);
    RECT rc;
    int w, i;
    SendMessageA(lv, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, LVS_EX_FULLROWSELECT | LVS_EX_CHECKBOXES | LVS_EX_SUBITEMIMAGES);
    SendMessageA(lv, LVM_SETIMAGELIST, LVSIL_SMALL, (LPARAM)g.swatches);
    GetClientRect(lv, &rc);
    w = rc.right - 4;
    lv_col(lv, 0, "Plot", w * 6 / 100, 0);
    lv_col(lv, 1, "Sensor", w * 34 / 100, 0);
    lv_col(lv, 2, "Current", w * 12 / 100, 1);
    lv_col(lv, 3, "Min", w * 12 / 100, 1);
    lv_col(lv, 4, "Max", w * 12 / 100, 1);
    lv_col(lv, 5, "Average", w * 12 / 100, 1);
    lv_col(lv, 6, "Alarm at", w * 12 / 100, 1);
    g.inUpdate++;
    for (i = 0; i < NSENS; i++) lv_add(lv, i, "");
    lv_select(lv, g.selSensor);
    g.inUpdate--;
    spin_init(GetDlgItem(d, IDC_LIMITSPIN));
}

static void sensors_refresh(void)
{
    HWND d = g.page[0], lv = GetDlgItem(d, IDC_SLIST);
    int i;
    char b[48];
    g.inUpdate++;
    for (i = 0; i < NSENS; i++) {
        Sensor *s = &g.s[i];
        lv_check(lv, i, s->plot && s->avail);
        lv_text(lv, i, 1, s->name);
        lv_image(lv, i, 1, s->avail ? i : NSENS);
        lv_image(lv, i, 0, -1);
        lv_text(lv, i, 2, s->avail ? app_ft(b, s->val) : "n/a");
        lv_text(lv, i, 3, s->avail ? app_ft(b, s->min) : "n/a");
        lv_text(lv, i, 4, s->avail ? app_ft(b, s->max) : "n/a");
        lv_text(lv, i, 5, s->avail ? app_ft(b, s->n ? s->sum / s->n : s->val) : "n/a");
        lv_text(lv, i, 6, app_ft(b, s->limit));
    }
    g.inUpdate--;
    wsprintfA(b, "Alarm limit for %s:", g.s[g.selSensor].name);
    set_text(GetDlgItem(d, IDC_ALARMLBL), b);
    set_text(GetDlgItem(d, IDC_LIMIT), app_ft(b, g.s[g.selSensor].limit));
    check(GetDlgItem(d, IDC_WARNWIN), g.o.warnWin);
    check(GetDlgItem(d, IDC_BEEP), g.o.beep);
    check(GetDlgItem(d, IDC_BALLOON), g.o.balloon && g.hw.os.hasBalloon);
    enable(GetDlgItem(d, IDC_BALLOON), g.hw.os.hasBalloon);
    InvalidateRect(GetDlgItem(d, IDC_GRAPH), 0, FALSE);
}

static LRESULT list_customdraw(NMLVCUSTOMDRAW *cd, int isFans)
{
    switch (cd->nmcd.dwDrawStage) {
    case CDDS_PREPAINT:
        return CDRF_NOTIFYITEMDRAW;
    case CDDS_ITEMPREPAINT:
        return CDRF_NOTIFYSUBITEMDRAW;
    case CDDS_ITEMPREPAINT | CDDS_SUBITEM: {
        int i = (int)cd->nmcd.dwItemSpec, sub = cd->iSubItem, avail, over = 0, boldCol;
        if (isFans) {
            if (i >= NFAN) return CDRF_DODEFAULT;
            avail = g.f[i].avail;
            over = g.f[i].avail && g.f[i].alarmOn && g.f[i].rpm < g.f[i].alarm;
            boldCol = 2;
        } else {
            if (i >= NSENS) return CDRF_DODEFAULT;
            avail = g.s[i].avail;
            over = avail && g.s[i].val > g.s[i].limit;
            boldCol = 2;
        }
        cd->clrText = !avail ? GetSysColor(COLOR_GRAYTEXT) : (over && sub == boldCol) ? RGB(192, 0, 0) : GetSysColor(COLOR_WINDOWTEXT);
        SelectObject(cd->nmcd.hdc, (sub == boldCol && avail) ? g.bold : g.font);
        return CDRF_NEWFONT;
    }
    }
    return CDRF_DODEFAULT;
}

/* ------------------------------------------------------------------ */
/* fans page                                                           */

static const int g_manualIds[] = { IDC_MANTXT, IDC_PCTDN, IDC_PCTBAR, IDC_PCTUP, IDC_PCTVAL, IDC_PL20, IDC_PL60, IDC_PL100 };
static const int g_profileIds[] = { IDC_PSILENT, IDC_PNORMAL, IDC_PFULL, IDC_PS1, IDC_PS2, IDC_PS3, IDC_PN1, IDC_PN2, IDC_PN3, IDC_PALL };
static const int g_curveIds[] = { IDC_FOLLOWLBL, IDC_FOLLOW, IDC_FOLLOWT, IDC_HYSTLBL, IDC_HYST, IDC_HYSTSPIN, IDC_CURVE, IDC_CMIN, IDC_CMAX, IDC_CTHDR, IDC_CDHDR };
static const int g_ncIds[] = { IDC_NCICON, IDC_NCTITLE, IDC_NCREASON };

static void show_ids(const int *ids, int n, int on)
{
    int i;
    for (i = 0; i < n; i++) show(pg(1, ids[i]), on);
}

static void fans_init(HWND d)
{
    HWND lv = GetDlgItem(d, IDC_FLIST);
    RECT rc;
    int w, i;
    SendMessageA(lv, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, LVS_EX_FULLROWSELECT);
    GetClientRect(lv, &rc);
    w = rc.right - 4;
    lv_col(lv, 0, "Fan", w * 28 / 100, 0);
    lv_col(lv, 1, "Connected to", w * 24 / 100, 0);
    lv_col(lv, 2, "Speed", w * 14 / 100, 1);
    lv_col(lv, 3, "Duty", w * 10 / 100, 1);
    lv_col(lv, 4, "Control", w * 24 / 100, 0);
    g.inUpdate++;
    for (i = 0; i < NFAN; i++) lv_add(lv, i, g.f[i].name);
    lv_select(lv, g.selFan);
    g.inUpdate--;
    SendMessageA(GetDlgItem(d, IDC_PCTBAR), TBM_SETRANGE, TRUE, MAKELONG(20, 100));
    SendMessageA(GetDlgItem(d, IDC_PCTBAR), TBM_SETPAGESIZE, 0, 5);
    spin_init(GetDlgItem(d, IDC_HYSTSPIN));
    spin_init(GetDlgItem(d, IDC_WARNSPIN));
    spin_init(GetDlgItem(d, IDC_FLOORSPIN));
    for (i = 0; i < NPTS; i++) {
        spin_init(GetDlgItem(d, IDC_PTTS1 + i));
        spin_init(GetDlgItem(d, IDC_PTDS1 + i));
    }
    SendDlgItemMessageA(d, IDC_NCICON, STM_SETICON, (WPARAM)LoadIcon(0, IDI_ASTERISK), 0);
    SendDlgItemMessageA(d, IDC_NCTITLE, WM_SETFONT, (WPARAM)g.bold, 0);
    SendDlgItemMessageA(d, IDC_RSTATE, WM_SETFONT, (WPARAM)g.bold, 0);
}

static void fill_follow(void)
{
    HWND cb = pg(1, IDC_FOLLOW);
    Fan *f = &g.f[g.selFan];
    int i, sel = -1, n = 0;
    SendMessageA(cb, CB_RESETCONTENT, 0, 0);
    for (i = 0; i < NSENS; i++) {
        if (!g.s[i].avail && f->follows != i) continue;
        SendMessageA(cb, CB_ADDSTRING, 0, (LPARAM)g.s[i].name);
        SendMessageA(cb, CB_SETITEMDATA, n, i);
        if (f->follows == i) sel = n;
        n++;
    }
    SendMessageA(cb, CB_ADDSTRING, 0, (LPARAM)"Hottest (CPU / GPU)");
    SendMessageA(cb, CB_SETITEMDATA, n, S_HOT);
    if (f->follows == S_HOT) sel = n;
    SendMessageA(cb, CB_SETCURSEL, sel, 0);
}

static void fans_refresh(int full)
{
    HWND d = g.page[1], lv = GetDlgItem(d, IDC_FLIST);
    Fan *f = &g.f[g.selFan];
    char b[200], n1[32], n2[32];
    int i, ctrl = f->ctrl;
    for (i = 0; i < NFAN; i++) {
        Fan *x = &g.f[i];
        lv_text(lv, i, 0, x->name);
        lv_text(lv, i, 1, x->header);
        if (x->avail) { wsprintfA(b, "%s RPM", rt_fmtint(n1, x->rpm)); lv_text(lv, i, 2, b); }
        else lv_text(lv, i, 2, "n/a");
        if (x->ctrl && x->duty >= 0) { wsprintfA(b, "%d %%", rt_round(x->duty)); lv_text(lv, i, 3, b); }
        else lv_text(lv, i, 3, x->ctrl ? "BIOS" : "-");
        if (x->ctrl) { mode_name(x, b); lv_text(lv, i, 4, b); }
        else lv_text(lv, i, 4, x->avail ? "Read only" : (g.hwState == HWS_OK ? "None" : "Unavailable"));
    }
    wsprintfA(b, "Speed control: %s", f->name);
    set_text(GetDlgItem(d, IDC_CTLGRP), b);
    for (i = 0; i < 4; i++) {
        HWND r = GetDlgItem(d, IDC_MAUTO + i);
        enable(r, ctrl);
        check(r, ctrl && f->mode == i);
    }
    show_ids(g_manualIds, COUNTOF(g_manualIds), ctrl && f->mode == M_MANUAL);
    show_ids(g_profileIds, COUNTOF(g_profileIds), ctrl && f->mode == M_PROFILE);
    show_ids(g_curveIds, COUNTOF(g_curveIds), ctrl && f->mode == M_CURVE);
    for (i = 0; i < NPTS; i++) {
        int on = ctrl && f->mode == M_CURVE;
        show(GetDlgItem(d, IDC_PTLBL1 + i), on);
        show(GetDlgItem(d, IDC_PTT1 + i), on);
        show(GetDlgItem(d, IDC_PTTS1 + i), on);
        show(GetDlgItem(d, IDC_PTD1 + i), on);
        show(GetDlgItem(d, IDC_PTDS1 + i), on);
    }
    show(GetDlgItem(d, IDC_AUTOTXT), ctrl && f->mode == M_AUTO);
    show_ids(g_ncIds, COUNTOF(g_ncIds), !ctrl);
    if (!ctrl) {
        fan_selectable_reason(f, b, sizeof(b));
        set_text(GetDlgItem(d, IDC_NCREASON), b);
    }
    if (ctrl && f->mode == M_MANUAL) {
        if (SendDlgItemMessageA(d, IDC_PCTBAR, TBM_GETPOS, 0, 0) != f->pct)
            SendDlgItemMessageA(d, IDC_PCTBAR, TBM_SETPOS, TRUE, f->pct);
        wsprintfA(b, "%d %%", f->pct);
        set_text(GetDlgItem(d, IDC_PCTVAL), b);
    }
    if (ctrl && f->mode == M_PROFILE) {
        for (i = 0; i < 3; i++) check(GetDlgItem(d, IDC_PSILENT + i), f->profile == i);
        check(GetDlgItem(d, IDC_PALL), g.o.applyAll);
    }
    if (ctrl && f->mode == M_CURVE) {
        if (full) fill_follow();
        if (g.o.hyst && f->hT - app_temp_of(f->follows) > 0.3)
            wsprintfA(b, "%s, holding %s", app_ft(n1, app_temp_of(f->follows)), app_ft(n2, f->hT));
        else
            app_ft(b, app_temp_of(f->follows));
        set_text(GetDlgItem(d, IDC_FOLLOWT), b);
        wsprintfA(b, "%d \xB0%c", g.unit == 'F' ? rt_round(g.o.hyst * 1.8) : g.o.hyst, g.unit);
        set_text(GetDlgItem(d, IDC_HYST), b);
        for (i = 0; i < NPTS; i++) {
            wsprintfA(b, "%d \xB0%c", rt_round(app_cv(f->pts[i][0])), g.unit);
            set_text(GetDlgItem(d, IDC_PTT1 + i), b);
            wsprintfA(b, "%d %%", f->pts[i][1]);
            set_text(GetDlgItem(d, IDC_PTD1 + i), b);
        }
        wsprintfA(b, "%d \xB0%c", rt_round(app_cv(20)), g.unit);
        set_text(GetDlgItem(d, IDC_CMIN), b);
        wsprintfA(b, "%d \xB0%c", rt_round(app_cv(80)), g.unit);
        set_text(GetDlgItem(d, IDC_CMAX), b);
        InvalidateRect(GetDlgItem(d, IDC_CURVE), 0, FALSE);
    }

    /* reading box */
    set_text(GetDlgItem(d, IDC_LED), f->avail ? rt_fmtint(b, f->rpm) : "----");
    {
        int fol = f->follows == S_HOT ? S_CPU : f->follows;
        if (g.s[fol].avail || f->follows == S_HOT)
            wsprintfA(b, "Sensor: %s %s", f->follows == S_HOT ? "Hottest" : g.s[fol].shortName, app_ft(n1, app_temp_of(f->follows)));
        else
            wsprintfA(b, "Sensor: %s n/a", g.s[fol].shortName);
        set_text(GetDlgItem(d, IDC_RSENSOR), b);
    }
    if (!f->avail) set_text(GetDlgItem(d, IDC_RTARGET), g.hwState == HWS_OK ? "No speed signal" : "Not available");
    else if (ctrl && f->duty >= 0) {
        wsprintfA(b, "Target %d %%, about %s RPM", rt_round(f->duty), rt_fmtint(n1, f->target));
        set_text(GetDlgItem(d, IDC_RTARGET), b);
    } else if (ctrl) set_text(GetDlgItem(d, IDC_RTARGET), "Set by the BIOS");
    else set_text(GetDlgItem(d, IDC_RTARGET), "Fixed speed, read only");
    if (!f->avail) set_text(GetDlgItem(d, IDC_RSTATE), "");
    else if (ctrl && f->duty >= 0 && f->target > 0 && rt_fabs((double)f->rpm - f->target) > f->target * 0.06)
        set_text(GetDlgItem(d, IDC_RSTATE), "Settling...");
    else set_text(GetDlgItem(d, IDC_RSTATE), "Steady");
    enable(GetDlgItem(d, IDC_WARNLOW), f->avail);
    enable(GetDlgItem(d, IDC_WARNRPM), f->avail);
    enable(GetDlgItem(d, IDC_WARNSPIN), f->avail);
    enable(GetDlgItem(d, IDC_WARNUNIT), f->avail);
    check(GetDlgItem(d, IDC_WARNLOW), f->avail && f->alarmOn);
    wsprintfA(b, "%d", f->alarm);
    set_text(GetDlgItem(d, IDC_WARNRPM), f->avail ? b : "");
    check(GetDlgItem(d, IDC_BIOSEXIT), g.o.bios);
    check(GetDlgItem(d, IDC_FAILSAFE), g.o.failsafe);
    wsprintfA(b, "%d %%", g.o.floor);
    set_text(GetDlgItem(d, IDC_FLOOR), b);
}

static void set_manual_pct(Fan *f, int v)
{
    char b[300];
    if (v < 20) v = 20;
    if (v > 100) v = 100;
    if (v < g.o.floor && !(f->allowBelow >= 0 && v >= f->allowBelow)) {
        wsprintfA(b, "Run %s at %d %%?\n\nThat is below the %d %% lowest duty set under Fan safety. Some fans stall at low duty, which lets the CPU overheat.",
                  f->name, v, g.o.floor);
        if (MessageBoxA(g.main, b, APP_NAME " - Low Fan Speed", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES) {
            f->allowBelow = v;
            f->pct = v;
        } else {
            f->pct = g.o.floor;
        }
    } else {
        f->pct = v;
    }
    app_apply_hw();
}

/* ------------------------------------------------------------------ */
/* logging page                                                        */

static void logging_init(HWND d)
{
    HWND lv = GetDlgItem(d, IDC_LVALS);
    RECT rc;
    int i, n = 0;
    char b[64];
    SendMessageA(lv, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT);
    GetClientRect(lv, &rc);
    lv_col(lv, 0, "Value", rc.right * 72 / 100, 0);
    lv_col(lv, 1, "Kind", rc.right * 24 / 100, 1);
    g.inUpdate++;
    for (i = 0; i < NSENS; i++) { lv_add(lv, n, g.s[i].name); lv_text(lv, n++, 1, "Temp"); }
    for (i = 0; i < NFAN; i++) { lv_add(lv, n, g.f[i].name); lv_text(lv, n++, 1, "RPM"); }
    for (i = 0; i < NFAN; i++) { wsprintfA(b, "%s duty", g.f[i].name); lv_add(lv, n, b); lv_text(lv, n++, 1, "%"); }
    g.inUpdate--;
    spin_init(GetDlgItem(d, IDC_LOGINTSPIN));
    SendDlgItemMessageA(d, IDC_LOGPATH, EM_LIMITTEXT, MAX_PATH - 1, 0);
}

static int g_lastRows = -1;

static void logging_refresh(void)
{
    HWND d = g.page[2], lv = GetDlgItem(d, IDC_LVALS);
    char b[1024], kb[24];
    const char *file = g.log.path, *p;
    int i, n = 0;
    for (p = g.log.path; *p; p++) if (*p == '\\') file = p + 1;
    if (GetFocus() != GetDlgItem(d, IDC_LOGPATH)) set_text(GetDlgItem(d, IDC_LOGPATH), g.log.path);
    enable(GetDlgItem(d, IDC_LOGPATH), !g.log.on);
    enable(GetDlgItem(d, IDC_BROWSE), !g.log.on);
    enable(GetDlgItem(d, IDC_FCSV), !g.log.on);
    enable(GetDlgItem(d, IDC_FTXT), !g.log.on);
    check(GetDlgItem(d, IDC_FCSV), g.log.csv);
    check(GetDlgItem(d, IDC_FTXT), !g.log.csv);
    wsprintfA(b, "%d", g.log.interval);
    set_text(GetDlgItem(d, IDC_LOGINT), b);
    log_line(b, sizeof(b), 1);
    set_text(GetDlgItem(d, IDC_EXAMPLE), b);
    check(GetDlgItem(d, IDC_LHEADER), g.log.csv && g.log.header);
    enable(GetDlgItem(d, IDC_LHEADER), g.log.csv);
    check(GetDlgItem(d, IDC_LROLL), g.log.roll);
    check(GetDlgItem(d, IDC_LSTART), g.log.onLaunch);
    g.inUpdate++;
    for (i = 0; i < NSENS; i++, n++) lv_check(lv, n, g.log.incS[i] && g.s[i].avail);
    for (i = 0; i < NFAN; i++, n++) lv_check(lv, n, g.log.incF[i] && g.f[i].avail);
    for (i = 0; i < NFAN; i++, n++) lv_check(lv, n, g.log.incD[i] && g.f[i].ctrl);
    g.inUpdate--;
    if (g_lastRows != g.log.rows + g.log.nrecent * 100000) {
        HWND l = GetDlgItem(d, IDC_LRECENT);
        g_lastRows = g.log.rows + g.log.nrecent * 100000;
        SendMessageA(l, WM_SETREDRAW, FALSE, 0);
        SendMessageA(l, LB_RESETCONTENT, 0, 0);
        if (!g.log.nrecent)
            SendMessageA(l, LB_ADDSTRING, 0, (LPARAM)"Nothing recorded yet. Press Start Logging to write readings to the file above.");
        for (i = 0; i < g.log.nrecent; i++) SendMessageA(l, LB_ADDSTRING, 0, (LPARAM)g.log.recent[i]);
        SendMessageA(l, LB_SETHORIZONTALEXTENT, 1600, 0);
        SendMessageA(l, LB_SETTOPINDEX, g.log.nrecent ? g.log.nrecent - 1 : 0, 0);
        SendMessageA(l, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(l, 0, TRUE);
    }
    if (g.log.error[0]) rt_cpy(b, g.log.error, sizeof(b));
    else if (g.log.rows)
        wsprintfA(b, "%s %ld rows written to %s (%s KB).%s", g.log.on ? "Recording." : "Stopped.", g.log.rows, file,
                  rt_fmtint(kb, (long)((g.log.bytes + 1023) / 1024)),
                  (g.log.on && g.log.unitAtStart != g.unit) ? " Temperatures stay in the old unit until logging restarts." : "");
    else rt_cpy(b, g.log.on ? "Recording." : "Not recording.", sizeof(b));
    set_text(GetDlgItem(d, IDC_LSUM), b);
    set_text(GetDlgItem(d, IDC_LBTN), g.log.on ? "Stop Logging" : "Start Logging");
}

static void browse_log(void)
{
    OPENFILENAMEA o;
    char file[MAX_PATH];
    rt_cpy(file, g.log.path, sizeof(file));
    ZERO(o);
    o.lStructSize = 76;                  /* the Windows 98 size of OPENFILENAME */
    o.hwndOwner = g.main;
    o.lpstrFilter = "CSV files (*.CSV)\0*.CSV\0Text files (*.TXT)\0*.TXT\0All files (*.*)\0*.*\0";
    o.nFilterIndex = g.log.csv ? 1 : 2;
    o.lpstrFile = file;
    o.nMaxFile = sizeof(file);
    o.lpstrDefExt = g.log.csv ? "CSV" : "TXT";
    o.lpstrTitle = "Log File";
    o.Flags = OFN_HIDEREADONLY | OFN_PATHMUSTEXIST | OFN_NOREADONLYRETURN;
    if (GetSaveFileNameA(&o)) {
        rt_cpy(g.log.path, file, sizeof(g.log.path));
        g.log.rows = 0;
    }
}

static void set_format(int csv)
{
    int n = rt_len(g.log.path), i;
    g.log.csv = csv;
    for (i = n - 1; i >= 0 && g.log.path[i] != '\\'; i--) {
        if (g.log.path[i] == '.') {
            g.log.path[i] = 0;
            break;
        }
    }
    rt_cat(g.log.path, csv ? ".CSV" : ".TXT", sizeof(g.log.path));
}

/* ------------------------------------------------------------------ */
/* system info page                                                    */

static const char *g_infoLabels[4][8] = {
    { "Name:", "Core:", "Clock:", "L1 / L2 cache:", "Features:", "Thermal diode:", "Core voltage:", 0 },
    { "Adapter:", "Memory:", "Chip:", "Driver:", "Display:", "Fan control:", "Temp sensor:", 0 },
    { "Chipset:", "Sensor chip:", "Chip address:", "Fan headers:", "+3.3 V / +5 V:", "+12 V:", "BIOS:", 0 },
    { "Windows:", "Memory:", "Temperatures:", "Fans:", "Sensor access:", "Log drive:", 0, 0 }
};

static void info_init(HWND d)
{
    static const int groups[4] = { IDC_GPROC, IDC_GGFX, IDC_GBOARD, IDC_GSYS };
    HFONT font = (HFONT)SendMessageA(d, WM_GETFONT, 0, 0);
    int gi, r;
    for (gi = 0; gi < 4; gi++) {
        RECT gr, u;
        int x, y, rowH, labW;
        GetWindowRect(GetDlgItem(d, groups[gi]), &gr);
        MapWindowPoints(0, d, (POINT *)&gr, 2);
        SetRect(&u, 8, 11, 70, 14);
        MapDialogRect(d, &u);
        x = gr.left + u.left;
        rowH = u.top;
        labW = u.right;
        y = gr.top + u.bottom;
        for (r = 0; r < 8 && g_infoLabels[gi][r]; r++) {
            HWND l = CreateWindowA("STATIC", g_infoLabels[gi][r], WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
                                   x, y + r * rowH, labW - 4, rowH, d, (HMENU)(IDC_IVAL + gi * 16 + r * 2), g.inst, 0);
            HWND v = CreateWindowA("STATIC", "", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX | SS_ENDELLIPSIS,
                                   x + labW, y + r * rowH, gr.right - x - labW - 6, rowH, d, (HMENU)(IDC_IVAL + gi * 16 + r * 2 + 1), g.inst, 0);
            SendMessageA(l, WM_SETFONT, (WPARAM)font, 0);
            SendMessageA(v, WM_SETFONT, (WPARAM)(r == 0 ? g.bold : font), 0);
        }
    }
}

static void info_val(int gi, int r, const char *s)
{
    set_text(GetDlgItem(g.page[3], IDC_IVAL + gi * 16 + r * 2 + 1), s);
}

static void log_drive(char *out)
{
    char root[8], fs[32];
    DWORD serial, maxlen, flags;
    if (g.log.path[1] == ':') {
        wsprintfA(root, "%c:\\", g.log.path[0]);
        if (GetVolumeInformationA(root, 0, 0, &serial, &maxlen, &flags, fs, sizeof(fs))) {
            wsprintfA(out, "%c: %s", g.log.path[0], fs);
            return;
        }
        wsprintfA(out, "%c: (not ready)", g.log.path[0]);
        return;
    }
    rt_cpy(out, "Network or other", 48);
}

static void info_refresh(int full)
{
    char b[160], v1[16], v2[16];
    int i, nt = 0, nf = 0, nc = 0;
    HwInfo *h = &g.hw;
    if (full) {
        info_val(0, 0, h->cpu.name);
        info_val(0, 1, h->cpu.core);
        if (h->cpu.mhz) wsprintfA(b, "%d MHz", h->cpu.mhz); else rt_cpy(b, "Unknown", sizeof(b));
        info_val(0, 2, b);
        info_val(0, 3, h->cpu.cache);
        wsprintfA(b, "%s%s%s%s", h->cpu.hasMmx ? "MMX " : "", h->cpu.hasSse ? "SSE " : "", h->cpu.hasTsc ? "TSC " : "",
                  (h->cpu.hasMmx || h->cpu.hasSse || h->cpu.hasTsc) ? "" : "None reported");
        info_val(0, 4, b);
        info_val(1, 0, h->gpu.name);
        info_val(1, 1, h->gpu.memory);
        info_val(1, 2, h->gpu.chip);
        info_val(1, 3, h->gpu.driver);
        info_val(1, 4, h->gpu.mode);
        info_val(1, 5, "Not available");
        info_val(1, 6, h->demo ? "Demo values" : "Not available");
        info_val(2, 0, h->chipset);
        if (h->chip.kind == CHIP_NONE)
            info_val(2, 1, h->ioAllowed ? "Not found" : "Unknown (no direct access)");
        else if (h->chip.supported) info_val(2, 1, h->chip.name);
        else { wsprintfA(b, "%s (not supported)", h->chip.name); info_val(2, 1, b); }
        info_val(2, 2, h->chip.where[0] ? h->chip.where : "-");
        if (h->chip.supported) {
            int nh = 0, nctl = 0;
            for (i = 0; i < NFAN_CHIP; i++) { nh += h->chip.hasFan[i]; nctl += h->chip.fanCtl[i]; }
            wsprintfA(b, "%d in use (%d with speed control)", nh, nctl);
            info_val(2, 3, b);
        } else info_val(2, 3, "Unknown");
        info_val(2, 6, h->bios);
        info_val(3, 0, h->os.name);
        wsprintfA(b, "%d MB", h->memMB);
        info_val(3, 1, b);
        if (h->demo) info_val(3, 4, "Demo mode (no hardware access)");
        else if (h->ioAllowed) info_val(3, 4, "Direct I/O (Windows 9x)");
        else if (h->os.isNT) info_val(3, 4, "Blocked by Windows NT family");
        else info_val(3, 4, "Turned off (/nohw)");
    }
    info_val(0, 5, g.s[S_CPU].avail ? "Yes, read via sensor chip" : "Not readable");
    if (g.rd.voltOk && h->chip.supported) {
        wsprintfA(b, "%s V", rt_fmt2(v1, g.rd.vcore));
        info_val(0, 6, b);
        wsprintfA(b, "%s V / %s V", rt_fmt2(v1, g.rd.v33), rt_fmt2(v2, g.rd.v5));
        info_val(2, 4, b);
        wsprintfA(b, "%s V", rt_fmt2(v1, g.rd.v12));
        info_val(2, 5, b);
    } else {
        info_val(0, 6, "n/a");
        info_val(2, 4, "n/a");
        info_val(2, 5, "n/a");
    }
    for (i = 0; i < NSENS; i++) nt += g.s[i].avail;
    for (i = 0; i < NFAN; i++) { nf += g.f[i].avail; nc += g.f[i].ctrl; }
    wsprintfA(b, "%d of %d readable", nt, NSENS);
    info_val(3, 2, b);
    wsprintfA(b, "%d report speed, %d controllable", nf, nc);
    info_val(3, 3, b);
    log_drive(b);
    info_val(3, 5, b);
    set_text(GetDlgItem(g.page[3], IDC_IMSG), g.reportMsg);
}

void ui_build_report(char *out, int cap)
{
    char b[200], t[24];
    int i;
    HwInfo *h = &g.hw;
    rt_cpy(out, "neoFanSpeed " APP_VER " hardware report\r\n\r\n", cap);
    wsprintfA(b, "Windows: %s\r\n", h->os.name); rt_cat(out, b, cap);
    wsprintfA(b, "CPU: %s, %d MHz\r\n", h->cpu.name, h->cpu.mhz); rt_cat(out, b, cap);
    wsprintfA(b, "CPU core: %s, stepping %d, vendor %s\r\n", h->cpu.core, h->cpu.stepping, h->cpu.vendor); rt_cat(out, b, cap);
    wsprintfA(b, "Cache: %s\r\n", h->cpu.cache); rt_cat(out, b, cap);
    wsprintfA(b, "GPU: %s, %s\r\n", h->gpu.name, h->gpu.memory); rt_cat(out, b, cap);
    wsprintfA(b, "Chipset: %s\r\n", h->chipset); rt_cat(out, b, cap);
    wsprintfA(b, "BIOS: %s\r\n", h->bios); rt_cat(out, b, cap);
    wsprintfA(b, "Memory: %d MB\r\n", h->memMB); rt_cat(out, b, cap);
    wsprintfA(b, "Sensor chip: %s %s (device id %Xh, kind %d)\r\n",
              h->chip.name[0] ? h->chip.name : "none", h->chip.where, h->chip.devId, h->chip.kind);
    rt_cat(out, b, cap);
    wsprintfA(b, "Direct I/O: %s\r\n\r\n", h->ioAllowed ? "yes" : "no"); rt_cat(out, b, cap);
    for (i = 0; i < NSENS; i++) {
        wsprintfA(b, "%s: %s\r\n", g.s[i].name, g.s[i].avail ? app_ft(t, g.s[i].val) : "n/a");
        rt_cat(out, b, cap);
    }
    for (i = 0; i < NFAN; i++) {
        if (g.f[i].avail) wsprintfA(b, "%s: %s RPM\r\n", g.f[i].name, rt_fmtint(t, g.f[i].rpm));
        else wsprintfA(b, "%s: n/a\r\n", g.f[i].name);
        rt_cat(out, b, cap);
    }
}

static void copy_report(void)
{
    char buf[4096];
    HGLOBAL mem;
    int n;
    ui_build_report(buf, sizeof(buf));
    n = rt_len(buf) + 1;
    if (!OpenClipboard(g.main)) return;
    EmptyClipboard();
    mem = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, n);
    if (mem) {
        memcpy(GlobalLock(mem), buf, n);
        GlobalUnlock(mem);
        SetClipboardData(CF_TEXT, mem);
    }
    CloseClipboard();
    rt_cpy(g.reportMsg, "Report copied to the clipboard.", sizeof(g.reportMsg));
}

static void save_report(void)
{
    OPENFILENAMEA o;
    char file[MAX_PATH], buf[4096];
    HANDLE h;
    DWORD w;
    rt_cpy(file, "REPORT.TXT", sizeof(file));
    ZERO(o);
    o.lStructSize = 76;
    o.hwndOwner = g.main;
    o.lpstrFilter = "Text files (*.TXT)\0*.TXT\0All files (*.*)\0*.*\0";
    o.lpstrFile = file;
    o.nMaxFile = sizeof(file);
    o.lpstrDefExt = "TXT";
    o.lpstrTitle = "Save Report";
    o.Flags = OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameA(&o)) return;
    ui_build_report(buf, sizeof(buf));
    h = CreateFileA(file, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (h == INVALID_HANDLE_VALUE) {
        rt_cpy(g.reportMsg, "Could not save the report.", sizeof(g.reportMsg));
        return;
    }
    WriteFile(h, buf, (DWORD)rt_len(buf), &w, 0);
    CloseHandle(h);
    wsprintfA(g.reportMsg, "Saved to %s", file);
}

/* ------------------------------------------------------------------ */
/* refresh everything that changes each second                         */

static void toolbar_refresh(void)
{
    int cur = app_cur_profile(), want;
    want = cur == -3 ? -1 : cur == -1 ? 3 : cur == -2 ? 4 : cur;
    enable(g.profile, cur != -3);
    if (cur == -3) {
        if (SendMessageA(g.profile, CB_GETCOUNT, 0, 0) != 1) {
            SendMessageA(g.profile, CB_RESETCONTENT, 0, 0);
            SendMessageA(g.profile, CB_ADDSTRING, 0, (LPARAM)"Unavailable");
        }
        want = 0;
    } else if (SendMessageA(g.profile, CB_GETCOUNT, 0, 0) != 5) {
        SendMessageA(g.profile, CB_RESETCONTENT, 0, 0);
        SendMessageA(g.profile, CB_ADDSTRING, 0, (LPARAM)"Silent");
        SendMessageA(g.profile, CB_ADDSTRING, 0, (LPARAM)"Normal");
        SendMessageA(g.profile, CB_ADDSTRING, 0, (LPARAM)"Full speed");
        SendMessageA(g.profile, CB_ADDSTRING, 0, (LPARAM)"BIOS control");
        SendMessageA(g.profile, CB_ADDSTRING, 0, (LPARAM)"Custom");
    }
    if (SendMessageA(g.profile, CB_GETCURSEL, 0, 0) != want && !SendMessageA(g.profile, CB_GETDROPPEDSTATE, 0, 0))
        SendMessageA(g.profile, CB_SETCURSEL, want, 0);
    InvalidateRect(g.logBtn, 0, FALSE);
}

void ui_refresh(void)
{
    if (!g.main) return;
    toolbar_refresh();
    if (g.curTab == 0) sensors_refresh();
    else if (g.curTab == 1) fans_refresh(0);
    else if (g.curTab == 2) logging_refresh();
    else info_refresh(0);
    ui_status();
    ui_banner();
    if (g.mini) mini_refresh();
}

void ui_refresh_static(void)
{
    if (!g.main) return;
    info_refresh(1);
    fans_refresh(1);
    ui_refresh();
}

/* ------------------------------------------------------------------ */
/* commands from the pages and the main window                         */

static void apply_detection(void)
{
    int i;
    HwInfo *h = &g.hw;
    if (h->demo) g.hwState = HWS_OK;
    else if (!h->ioAllowed && h->os.isNT) g.hwState = HWS_NT;
    else if (h->chip.kind == CHIP_NONE) g.hwState = HWS_NOCHIP;
    else if (!h->chip.supported) g.hwState = HWS_UNSUPPORTED;
    else g.hwState = HWS_OK;
    if (!h->ioAllowed && !h->demo && !h->os.isNT) g.hwState = HWS_NOCHIP;
    for (i = 0; i < NFAN; i++) {
        g.f[i].ctrl = h->chip.supported && h->chip.canControl && h->chip.fanCtl[i];
        g.f[i].lastWritten = -2;
        g.f[i].maxRpm = 0;
    }
    for (i = 0; i < NSENS; i++) g.s[i].n = 0;
    app_tick(0);
    app_reset_stats();
    ui_refresh_static();
}

static void on_command(int id, int code, HWND ctl)
{
    Fan *f = &g.f[g.selFan];
    (void)ctl;
    switch (id) {
    case IDM_LOGTOGGLE: case IDC_LOGBTN: case IDC_LBTN:
        log_toggle();
        break;
    case IDM_RESET: case IDC_RESETBTN:
        app_reset_stats();
        break;
    case IDM_TRAY:
        ShowWindow(g.main, SW_HIDE);
        if (g.mini) ShowWindow(g.mini, SW_HIDE);
        return;
    case IDM_EXIT:
        g.exiting = 1;
        DestroyWindow(g.main);
        return;
    case IDM_CELSIUS: g.unit = 'C'; break;
    case IDM_FAHRENHEIT: g.unit = 'F'; break;
    case IDM_ONTOP:
        g.onTop = !g.onTop;
        SetWindowPos(g.main, g.onTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        if (g.mini) SetWindowPos(g.mini, g.onTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        break;
    case IDM_MINI:
        g.miniView = 1;
        mini_create();
        ShowWindow(g.main, SW_HIDE);
        ShowWindow(g.mini, SW_SHOW);
        break;
    case IDM_DETECT: case IDC_IDETECT:
        ui_detect(g.main);
        break;
    case IDM_ABOUT:
        MessageBoxA(g.main, "neoFanSpeed " APP_VER "\n\nTemperature, fan speed and system details for Windows 98, 98SE, ME, 2000 and XP.\n\n"
                    "Fan control works on Windows 98, 98SE and ME with a supported Winbond sensor chip.\n\n"
                    "Start with /demo to try it with simulated readings.",
                    "About neoFanSpeed", MB_OK | MB_ICONINFORMATION);
        return;
    case IDM_SHOW:
        if (g.miniView && g.mini) ShowWindow(g.mini, SW_SHOW);
        else { ShowWindow(g.main, SW_SHOW); ShowWindow(g.main, SW_RESTORE); SetForegroundWindow(g.main); }
        return;
    case IDM_PSILENT: app_set_all(P_SILENT); break;
    case IDM_PNORMAL: app_set_all(P_NORMAL); break;
    case IDM_PFULL: app_set_all(P_FULL); break;
    case IDM_PBIOS: app_set_all(-1); break;
    case IDM_TAB_SENSORS: ui_set_tab(0); return;
    case IDM_TAB_FANS: ui_set_tab(1); return;
    case IDM_TAB_LOGGING: ui_set_tab(2); return;
    case IDM_TAB_INFO: ui_set_tab(3); return;

    case IDC_PROFILE:
        if (code == CBN_SELCHANGE && app_count_ctrl()) {
            int s = (int)SendMessageA(g.profile, CB_GETCURSEL, 0, 0);
            if (s >= 0 && s <= 2) app_set_all(s);
            else if (s == 3) app_set_all(-1);
        }
        break;
    case IDC_BANNERBTN:
        if (g.fsActive && !g.alarmDlg) app_restore_prev();
        else if (g.hwState == HWS_NT) run_as_admin();
        else if (g.hwState == HWS_NOCHIP) ui_detect(g.main);
        else if (g.hwState == HWS_UNSUPPORTED) save_chip_report();
        break;

    case IDC_WARNWIN: g.o.warnWin = checked(ctl); break;
    case IDC_BEEP: g.o.beep = checked(ctl); break;
    case IDC_BALLOON: g.o.balloon = checked(ctl); break;

    case IDC_MAUTO: case IDC_MMANUAL: case IDC_MPROFILE: case IDC_MCURVE:
        if (code == BN_CLICKED && f->ctrl) {
            int m = id - IDC_MAUTO;
            if (m == M_MANUAL && f->mode != M_MANUAL) {
                int p = f->duty >= 0 ? rt_round(f->duty / 5) * 5 : 65;
                f->pct = p < g.o.floor ? g.o.floor : p > 100 ? 100 : p;
            }
            f->mode = m;
            g.fsActive = 0;
            app_apply_hw();
            fans_refresh(1);
        }
        break;
    case IDC_PCTDN: if (f->ctrl) set_manual_pct(f, f->pct - 5); break;
    case IDC_PCTUP: if (f->ctrl) set_manual_pct(f, f->pct + 5); break;
    case IDC_PSILENT: case IDC_PNORMAL: case IDC_PFULL:
        if (code == BN_CLICKED && f->ctrl) {
            int p = id - IDC_PSILENT;
            if (g.o.applyAll) app_set_all(p);
            else { f->profile = p; app_apply_hw(); }
        }
        break;
    case IDC_PALL: g.o.applyAll = checked(ctl); break;
    case IDC_FOLLOW:
        if (code == CBN_SELCHANGE) {
            int s = (int)SendMessageA(ctl, CB_GETCURSEL, 0, 0);
            if (s >= 0) {
                f->follows = (int)SendMessageA(ctl, CB_GETITEMDATA, s, 0);
                f->hT = -1000;
                app_apply_hw();
            }
        }
        break;
    case IDC_WARNLOW: f->alarmOn = checked(ctl); break;
    case IDC_BIOSEXIT: g.o.bios = checked(ctl); break;
    case IDC_FAILSAFE: g.o.failsafe = checked(ctl); break;

    case IDC_LOGPATH:
        if (code == EN_KILLFOCUS && !g.log.on) {
            char b[MAX_PATH];
            GetWindowTextA(ctl, b, sizeof(b));
            rt_trim(b);
            if (b[0] && !rt_ieq(b, g.log.path)) { rt_cpy(g.log.path, b, sizeof(g.log.path)); g.log.rows = 0; }
        }
        return;
    case IDC_BROWSE: browse_log(); break;
    case IDC_FCSV: if (code == BN_CLICKED && !g.log.csv) set_format(1); break;
    case IDC_FTXT: if (code == BN_CLICKED && g.log.csv) set_format(0); break;
    case IDC_LHEADER: g.log.header = checked(ctl); break;
    case IDC_LROLL: g.log.roll = checked(ctl); break;
    case IDC_LSTART: g.log.onLaunch = checked(ctl); break;

    case IDC_ICOPY: copy_report(); break;
    case IDC_ISAVE: save_report(); break;
    default:
        return;
    }
    ui_refresh();
}

static void on_spin(int id, int delta)
{
    Fan *f = &g.f[g.selFan];
    int step = delta > 0 ? 1 : -1, i;
    switch (id) {
    case IDC_LIMITSPIN: {
        Sensor *s = &g.s[g.selSensor];
        s->limit += step;
        if (s->limit < 25) s->limit = 25;
        if (s->limit > 105) s->limit = 105;
        s->alerted = 0;
        break;
    }
    case IDC_HYSTSPIN:
        g.o.hyst += step;
        if (g.o.hyst < 0) g.o.hyst = 0;
        if (g.o.hyst > 10) g.o.hyst = 10;
        break;
    case IDC_WARNSPIN:
        if (!f->avail) return;
        f->alarm += step * 100;
        if (f->alarm < 0) f->alarm = 0;
        if (f->alarm > 10000) f->alarm = 10000;
        break;
    case IDC_FLOORSPIN:
        g.o.floor += step * 5;
        if (g.o.floor < 20) g.o.floor = 20;
        if (g.o.floor > 50) g.o.floor = 50;
        for (i = 0; i < NFAN; i++) g.f[i].allowBelow = -1;
        app_apply_hw();
        break;
    case IDC_LOGINTSPIN: {
        int k = 0;
        for (i = 0; i < (int)COUNTOF(g_intervals); i++) if (g_intervals[i] == g.log.interval) k = i;
        k += step;
        if (k < 0) k = 0;
        if (k >= (int)COUNTOF(g_intervals)) k = COUNTOF(g_intervals) - 1;
        g.log.interval = g_intervals[k];
        break;
    }
    default:
        if (id >= IDC_PTTS1 && id < IDC_PTTS1 + NPTS) {
            int k = id - IDC_PTTS1, lo = k > 0 ? f->pts[k - 1][0] + 1 : 20, hi = k < NPTS - 1 ? f->pts[k + 1][0] - 1 : 90;
            f->pts[k][0] += step;
            if (f->pts[k][0] < lo) f->pts[k][0] = lo;
            if (f->pts[k][0] > hi) f->pts[k][0] = hi;
        } else if (id >= IDC_PTDS1 && id < IDC_PTDS1 + NPTS) {
            int k = id - IDC_PTDS1;
            f->pts[k][1] += step * 5;
            if (f->pts[k][1] < g.o.floor) f->pts[k][1] = g.o.floor;
            if (f->pts[k][1] > 100) f->pts[k][1] = 100;
        } else return;
        app_apply_hw();
        break;
    }
    ui_refresh();
}

static LRESULT on_notify(int page, NMHDR *nm)
{
    if (nm->code == UDN_DELTAPOS) {
        on_spin((int)nm->idFrom, ((NMUPDOWN *)nm)->iDelta);
        return TRUE;                     /* keep the arrow in the middle */
    }
    if (nm->code == NM_CUSTOMDRAW && (nm->idFrom == IDC_SLIST || nm->idFrom == IDC_FLIST))
        return list_customdraw((NMLVCUSTOMDRAW *)nm, nm->idFrom == IDC_FLIST);
    if (nm->code == LVN_ITEMCHANGED && !g.inUpdate) {
        NMLISTVIEW *lv = (NMLISTVIEW *)nm;
        int i = lv->iItem, stateChange = ((lv->uNewState ^ lv->uOldState) & LVIS_STATEIMAGEMASK) != 0;
        int sel = (lv->uNewState & LVIS_SELECTED) && !(lv->uOldState & LVIS_SELECTED);
        if (i < 0) return 0;
        if (nm->idFrom == IDC_SLIST && i < NSENS) {
            if (stateChange && lv->uOldState & LVIS_STATEIMAGEMASK) {
                if (g.s[i].avail) g.s[i].plot = lv_checked(nm->hwndFrom, i);
                PostMessageA(g.main, WM_APP + 2, 0, 0);
            }
            if (sel && g.s[i].avail) g.selSensor = i;
            if (sel || stateChange) ui_refresh();
        } else if (nm->idFrom == IDC_FLIST && i < NFAN) {
            if (sel) { g.selFan = i; fans_refresh(1); }
        } else if (nm->idFrom == IDC_LVALS && stateChange && lv->uOldState & LVIS_STATEIMAGEMASK) {
            int on = lv_checked(nm->hwndFrom, i);
            if (i < NSENS) { if (g.s[i].avail) g.log.incS[i] = on; }
            else if (i < NSENS + NFAN) { if (g.f[i - NSENS].avail) g.log.incF[i - NSENS] = on; }
            else if (i < NSENS + 2 * NFAN) { if (g.f[i - NSENS - NFAN].ctrl) g.log.incD[i - NSENS - NFAN] = on; }
            PostMessageA(g.main, WM_APP + 2, 0, 0);
        }
    }
    (void)page;
    return 0;
}

/* ------------------------------------------------------------------ */
/* page dialog procedure                                               */

typedef HRESULT (WINAPI *ENABLETHEMEDLG_)(HWND, DWORD);

static INT_PTR CALLBACK page_proc(HWND d, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_INITDIALOG: {
        HMODULE ux = LoadLibraryA("UXTHEME.DLL");
        int p = (int)lp;
        g.page[p] = d;
        if (ux) {
            ENABLETHEMEDLG_ fn = (ENABLETHEMEDLG_)GetProcAddress(ux, "EnableThemeDialogTexture");
            if (fn) fn(d, 6 /* ETDT_ENABLETAB */);
        }
        if (p == 0) sensors_init(d);
        else if (p == 1) fans_init(d);
        else if (p == 2) logging_init(d);
        else info_init(d);
        return FALSE;
    }
    case WM_COMMAND:
        on_command(LOWORD(wp), HIWORD(wp), (HWND)lp);
        return TRUE;
    case WM_NOTIFY: {
        int p;
        for (p = 0; p < 4 && g.page[p] != d; p++) {}
        SetWindowLongA(d, DWL_MSGRESULT, (LONG)on_notify(p, (NMHDR *)lp));
        return TRUE;
    }
    case WM_HSCROLL:
        if ((HWND)lp == GetDlgItem(d, IDC_PCTBAR)) {
            Fan *f = &g.f[g.selFan];
            int v = (int)SendMessageA((HWND)lp, TBM_GETPOS, 0, 0);
            WORD code = LOWORD(wp);
            if (code == TB_ENDTRACK || code == TB_THUMBPOSITION || code == TB_PAGEUP ||
                code == TB_PAGEDOWN || code == TB_LINEUP || code == TB_LINEDOWN || code == TB_THUMBTRACK) {
                if (f->ctrl && v != f->pct) {
                    if ((code == TB_THUMBTRACK || code == TB_THUMBPOSITION) && v < g.o.floor) return TRUE;
                    set_manual_pct(f, v);
                    fans_refresh(0);
                }
            }
            return TRUE;
        }
        break;
    }
    return FALSE;
}

/* ------------------------------------------------------------------ */
/* alarm window                                                        */

static INT_PTR CALLBACK alarm_proc(HWND d, UINT m, WPARAM wp, LPARAM lp)
{
    (void)lp;
    switch (m) {
    case WM_INITDIALOG: {
        char a[200], t1[24], t2[24];
        SendDlgItemMessageA(d, IDC_AICON, STM_SETICON, (WPARAM)LoadIcon(0, IDI_EXCLAMATION), 0);
        if (g.alarmIsFan) {
            Fan *f = &g.f[g.alarmSensor];
            SetWindowTextA(d, APP_NAME " - Fan Speed Alarm");
            wsprintfA(a, "%s is at %s RPM, below its alarm limit of %s RPM.",
                      f->name, rt_fmtint(t1, (int)g.alarmTemp), rt_fmtint(t2, f->alarm));
        } else {
            Sensor *s = &g.s[g.alarmSensor];
            wsprintfA(a, "%s is at %s, above its alarm limit of %s.", s->name, app_ft(t1, g.alarmTemp), app_ft(t2, s->limit));
        }
        SetDlgItemTextA(d, IDC_ATEXT, a);
        if (g.fsActive) {
            SetDlgItemTextA(d, IDC_ATEXT2, "All controllable fans were set to full speed.");
            SetDlgItemTextA(d, IDC_ARESTORE, "Restore My Settings");
        } else if (app_count_ctrl()) {
            SetDlgItemTextA(d, IDC_ATEXT2, "Fan speeds were not changed.");
            SetDlgItemTextA(d, IDC_ARESTORE, "Set Fans to Full");
        } else {
            SetDlgItemTextA(d, IDC_ATEXT2, "No fans here can be controlled. Check the cooling.");
            ShowWindow(GetDlgItem(d, IDC_ARESTORE), SW_HIDE);
        }
        if (g.onTop) SetWindowPos(d, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_ARESTORE) {
            if (g.fsActive) app_restore_prev();
            else if (g.alarmIsFan) app_force_full_fan(g.alarmSensor);
            else app_force_full(g.alarmSensor);
        }
        if (LOWORD(wp) == IDOK || LOWORD(wp) == IDCANCEL || LOWORD(wp) == IDC_ARESTORE) {
            DestroyWindow(d);
            g.alarmDlg = 0;
            ui_refresh();
        }
        return TRUE;
    }
    return FALSE;
}

void ui_show_alarm(int id, int isFan)
{
    if (g.alarmDlg) return;
    g.alarmSensor = id;
    g.alarmIsFan = isFan;
    g.alarmDlg = CreateDialogParamA(g.inst, MAKEINTRESOURCEA(IDD_ALARM), g.main, alarm_proc, 0);
    if (g.alarmDlg) {
        ShowWindow(g.alarmDlg, SW_SHOW);
        SetForegroundWindow(g.alarmDlg);
    }
}

/* ------------------------------------------------------------------ */
/* hardware detection window                                           */

static int g_detStep;
static int g_detBad[5];

static INT_PTR CALLBACK detect_proc(HWND d, UINT m, WPARAM wp, LPARAM lp)
{
    static const char *labels[5] = {
        "Checking hardware access", "Looking for a sensor chip", "Reading the CPU and graphics card",
        "Reading hard disks (S.M.A.R.T.)", "Testing fan headers"
    };
    char b[128];
    int i;
    switch (m) {
    case WM_INITDIALOG:
        g_detStep = 0;
        for (i = 0; i < 5; i++) { SetDlgItemTextA(d, 1801 + i, labels[i]); g_detBad[i] = 0; }
        SendDlgItemMessageA(d, IDC_DPROG, PBM_SETRANGE, 0, MAKELPARAM(0, 5));
        EnableWindow(GetDlgItem(d, IDOK), FALSE);
        SetDlgItemTextA(d, 1811, "Checking...");
        SetTimer(d, 2, 350, 0);
        return TRUE;
    case WM_TIMER: {
        HwInfo *h = &g.hw;
        HCURSOR oc = SetCursor(LoadCursor(0, IDC_WAIT));
        b[0] = 0;
        switch (g_detStep) {
        case 0:
            if (h->demo) rt_cpy(b, "Demo mode", sizeof(b));
            else if (h->ioAllowed) rt_cpy(b, "OK (direct I/O)", sizeof(b));
            else if (h->os.isNT) { wsprintfA(b, "Blocked by Windows %s", h->os.shortName); g_detBad[0] = 1; }
            else { rt_cpy(b, "Turned off", sizeof(b)); g_detBad[0] = 1; }
            break;
        case 1:
            hw_chip_detect(h);
            if (h->chip.kind == CHIP_NONE) { rt_cpy(b, h->ioAllowed || h->demo ? "Not found" : "Skipped", sizeof(b)); g_detBad[1] = h->ioAllowed; }
            else if (!h->chip.supported) { wsprintfA(b, "%s, not supported", h->chip.name); g_detBad[1] = 1; }
            else if (h->chip.where[0] && h->chip.kind != CHIP_DEMO) wsprintfA(b, "%s at %Xh", h->chip.name, h->chip.base);
            else rt_cpy(b, h->chip.name, sizeof(b));
            break;
        case 2:
            hw_cpu(&h->cpu);
            hw_gpu(&h->gpu);
            hw_board(h);
            rt_cpy(b, h->gpu.name, sizeof(b));
            break;
        case 3:
            hw_smart_scan(h);
            if (h->smartDrives) wsprintfA(b, "%d drive%s", h->smartDrives, h->smartDrives > 1 ? "s" : "");
            else if (h->os.isNT && !h->os.isAdmin) { rt_cpy(b, "Needs administrator", sizeof(b)); g_detBad[3] = 1; }
            else rt_cpy(b, "No temperature found", sizeof(b));
            break;
        case 4: {
            int nf = 0, nc = 0;
            for (i = 0; i < NFAN_CHIP; i++) { nf += h->chip.hasFan[i]; nc += h->chip.fanCtl[i] && h->chip.canControl; }
            if (!h->chip.supported) rt_cpy(b, "Skipped", sizeof(b));
            else wsprintfA(b, "%d of %d controllable", nc, nf);
            break;
        }
        }
        SetCursor(oc);
        SetDlgItemTextA(d, 1811 + g_detStep, b);
        g_detStep++;
        SendDlgItemMessageA(d, IDC_DPROG, PBM_SETPOS, g_detStep, 0);
        if (g_detStep < 5) SetDlgItemTextA(d, 1811 + g_detStep, "Checking...");
        else {
            KillTimer(d, 2);
            EnableWindow(GetDlgItem(d, IDOK), TRUE);
            SetFocus(GetDlgItem(d, IDOK));
        }
        return TRUE;
    }
    case WM_CTLCOLORSTATIC: {
        int id = GetDlgCtrlID((HWND)lp);
        if (id >= 1801 && id <= 1815 || id == IDC_DBOX) {
            SetBkColor((HDC)wp, GetSysColor(COLOR_WINDOW));
            if (id >= 1811 && g_detBad[id - 1811]) SetTextColor((HDC)wp, RGB(192, 0, 0));
            else SetTextColor((HDC)wp, GetSysColor(COLOR_WINDOWTEXT));
            if (id >= 1811) SelectObject((HDC)wp, g.bold);
            return (INT_PTR)GetSysColorBrush(COLOR_WINDOW);
        }
        break;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK && g_detStep >= 5) EndDialog(d, IDOK);
        return TRUE;
    }
    return FALSE;
}

void ui_detect(HWND owner)
{
    hw_chip_restore(&g.hw);
    DialogBoxParamA(g.inst, MAKEINTRESOURCEA(IDD_DETECT), owner, detect_proc, 0);
    apply_detection();
}

/* ------------------------------------------------------------------ */
/* main window                                                         */

void ui_draw_log_button(DRAWITEMSTRUCT *di, const char *label)
{
    RECT r = di->rcItem, dot;
    UINT st = DFCS_BUTTONPUSH;
    HBRUSH br;
    int pushed = (di->itemState & ODS_SELECTED) != 0;
    if (pushed) st |= DFCS_PUSHED;
    DrawFrameControl(di->hDC, &r, DFC_BUTTON, st);
    SetRect(&dot, r.left + 8, (r.top + r.bottom) / 2 - 3, r.left + 15, (r.top + r.bottom) / 2 + 4);
    if (pushed) OffsetRect(&dot, 1, 1);
    br = CreateSolidBrush(g.log.on ? RGB(0, 0, 0) : RGB(192, 0, 0));
    SelectObject(di->hDC, br);
    SelectObject(di->hDC, GetStockObject(NULL_PEN));
    if (g.log.on) FillRect(di->hDC, &dot, br);
    else Ellipse(di->hDC, dot.left, dot.top, dot.right + 1, dot.bottom + 1);
    DeleteObject(br);
    r.left += 20;
    if (pushed) OffsetRect(&r, 1, 1);
    SetBkMode(di->hDC, TRANSPARENT);
    SetTextColor(di->hDC, GetSysColor(COLOR_BTNTEXT));
    SelectObject(di->hDC, g.font);
    DrawTextA(di->hDC, label, -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    if (di->itemState & ODS_FOCUS) {
        RECT f = di->rcItem;
        InflateRect(&f, -4, -4);
        DrawFocusRect(di->hDC, &f);
    }
}

static void create_children(HWND w)
{
    TCITEMA ti;
    static const char *tabs[4] = { "Sensors", "Fans", "Logging", "System Info" };
    RECT pr;
    int i;
    g.logBtn = CreateWindowA("BUTTON", "", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 6, 5, 104, 25, w, (HMENU)IDC_LOGBTN, g.inst, 0);
    g.resetBtn = CreateWindowA("BUTTON", "&Reset Min/Max", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 114, 5, 100, 25, w, (HMENU)IDC_RESETBTN, g.inst, 0);
    g.profLbl = CreateWindowA("STATIC", "Fan profile:", WS_CHILD | WS_VISIBLE, 230, 10, 64, 16, w, (HMENU)IDC_PROFLBL, g.inst, 0);
    g.profile = CreateWindowA("COMBOBOX", "", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, 296, 6, 124, 160, w, (HMENU)IDC_PROFILE, g.inst, 0);
    g.banner = CreateWindowA("NFSBanner", "", WS_CHILD | WS_CLIPCHILDREN, 6, 36, 600, 50, w, (HMENU)IDC_BANNER, g.inst, 0);
    g.bannerBtn = CreateWindowA("BUTTON", "Scan Again", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 440, 12, 146, 25, g.banner, (HMENU)IDC_BANNERBTN, g.inst, 0);
    g.tab = CreateWindowA(WC_TABCONTROLA, "", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP, 6, 36, 600, 400, w, (HMENU)IDC_TAB, g.inst, 0);
    g.status = CreateStatusWindowA(WS_CHILD | WS_VISIBLE, "", w, IDC_STATUS);
    SendMessageA(g.resetBtn, WM_SETFONT, (WPARAM)g.font, 0);
    SendMessageA(g.profLbl, WM_SETFONT, (WPARAM)g.font, 0);
    SendMessageA(g.profile, WM_SETFONT, (WPARAM)g.font, 0);
    SendMessageA(g.bannerBtn, WM_SETFONT, (WPARAM)g.font, 0);
    SendMessageA(g.tab, WM_SETFONT, (WPARAM)g.font, 0);
    SendMessageA(g.status, WM_SETFONT, (WPARAM)g.font, 0);
    for (i = 0; i < 4; i++) {
        ZERO(ti);
        ti.mask = TCIF_TEXT;
        ti.pszText = (char *)tabs[i];
        SendMessageA(g.tab, TCM_INSERTITEMA, i, (LPARAM)&ti);
    }
    for (i = 0; i < 4; i++)
        CreateDialogParamA(g.inst, MAKEINTRESOURCEA(IDD_SENSORS + i), w, page_proc, i);
    SendMessageA(g.logBtn, WM_SETFONT, (WPARAM)g.font, 0);
    GetWindowRect(g.page[0], &pr);
    g_pageW = pr.right - pr.left;
    g_pageH = pr.bottom - pr.top;
    SetRect(&pr, 0, 0, 1000, 1000);
    TabCtrl_AdjustRect(g.tab, FALSE, &pr);
    g_tabExtraW = 1000 - (pr.right - pr.left) + 4;
    g_tabExtraH = 1000 - (pr.bottom - pr.top) + 4;
}

static void do_exit_cleanup(void)
{
    KillTimer(g.main, TIMER_ID);
    if (g.o.bios) hw_chip_restore(&g.hw);
    app_save_settings();
    tray_remove();
}

static LRESULT CALLBACK main_proc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
    if (m == g.taskbarMsg && g.taskbarMsg) {
        g.trayAdded = 0;
        ui_tray_update();
        return 0;
    }
    switch (m) {
    case WM_CREATE:
        g.main = w;
        create_children(w);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(w, &ps);
        RECT r;
        SetRect(&r, 222, 6, 224, 30);
        DrawEdge(dc, &r, EDGE_ETCHED, BF_LEFT);
        EndPaint(w, &ps);
        return 0;
    }
    case WM_DRAWITEM:
        if (wp == IDC_LOGBTN) {
            ui_draw_log_button((DRAWITEMSTRUCT *)lp, g.log.on ? "S&top Logging" : "S&tart Logging");
            return TRUE;
        }
        break;
    case WM_TIMER:
        if (wp == TIMER_ID) {
            app_tick(1);
            ui_refresh();
            ui_tray_update();
        }
        return 0;
    case WM_APP + 2:
        ui_refresh();
        return 0;
    case WM_NOTIFY: {
        NMHDR *nm = (NMHDR *)lp;
        if (nm->idFrom == IDC_TAB && nm->code == TCN_SELCHANGE) ui_set_tab(TabCtrl_GetCurSel(g.tab));
        return 0;
    }
    case WM_COMMAND:
        on_command(LOWORD(wp), HIWORD(wp), (HWND)lp);
        return 0;
    case WM_INITMENUPOPUP: {
        HMENU mm = (HMENU)wp;
        CheckMenuItem(mm, IDM_CELSIUS, g.unit == 'C' ? MF_CHECKED : MF_UNCHECKED);
        CheckMenuItem(mm, IDM_FAHRENHEIT, g.unit == 'F' ? MF_CHECKED : MF_UNCHECKED);
        CheckMenuItem(mm, IDM_ONTOP, g.onTop ? MF_CHECKED : MF_UNCHECKED);
        ModifyMenuA(mm, IDM_LOGTOGGLE, MF_BYCOMMAND | MF_STRING, IDM_LOGTOGGLE, g.log.on ? "S&top Logging\tAlt+T" : "S&tart Logging\tAlt+T");
        return 0;
    }
    case WM_TRAY:
        if (lp == WM_LBUTTONDBLCLK || lp == WM_LBUTTONUP) on_command(IDM_SHOW, 0, 0);
        else if (lp == WM_RBUTTONUP) tray_menu();
        return 0;
    case WM_CLOSE:
        if (g.trayAdded && !g.exiting) {
            ShowWindow(w, SW_HIDE);
            return 0;
        }
        g.exiting = 1;
        DestroyWindow(w);
        return 0;
    case WM_QUERYENDSESSION:
        return TRUE;
    case WM_ENDSESSION:
        if (wp) do_exit_cleanup();
        return 0;
    case WM_DESTROY:
        do_exit_cleanup();
        if (g.mini) DestroyWindow(g.mini);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(w, m, wp, lp);
}

/* ------------------------------------------------------------------ */
/* program start                                                       */

typedef BOOL (WINAPI *INITCCEX_)(const INITCOMMONCONTROLSEX *);

static INT_PTR CALLBACK null_proc(HWND d, UINT m, WPARAM wp, LPARAM lp) { return m == WM_INITDIALOG; }

/* the font the dialogs get: MS Sans Serif on 98 and ME, Tahoma on 2000 and XP */
static HFONT dialog_font(void)
{
    HWND d = CreateDialogParamA(g.inst, MAKEINTRESOURCEA(IDD_ALARM), 0, null_proc, 0);
    HFONT f = 0;
    LOGFONTA lf;
    if (d) {
        HFONT df = (HFONT)SendMessageA(d, WM_GETFONT, 0, 0);
        if (df && GetObjectA(df, sizeof(lf), &lf)) f = CreateFontIndirectA(&lf);
        DestroyWindow(d);
    }
    return f ? f : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
}

static int has_arg(const char *cmd, const char *a)
{
    const char *p = cmd;
    while ((p = rt_find(p, a)) != 0) {
        char before = p == cmd ? ' ' : p[-1], after = p[rt_len(a)];
        if ((before == ' ' || before == '"') && (after == 0 || after == ' ' || after == '"')) return 1;
        p++;
    }
    return 0;
}

int nfs_main(void)
{
    WNDCLASSA wc;
    MSG msg;
    HMODULE cc;
    LOGFONTA lf;
    HANDLE once;
    const char *cmd = GetCommandLineA();
    char *p;
    int i;

    ZERO(g);
    g.inst = GetModuleHandleA(0);
    once = CreateMutexA(0, FALSE, "neoFanSpeed.Once");
    if (once && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND other = FindWindowA("neoFanSpeedMain", 0);
        if (other) PostMessageA(other, WM_COMMAND, IDM_SHOW, 0);
        return 0;
    }
    rt_srand(GetTickCount());

    InitCommonControls();
    cc = GetModuleHandleA("COMCTL32.DLL");
    if (cc) {
        INITCCEX_ fn = (INITCCEX_)GetProcAddress(cc, "InitCommonControlsEx");
        if (fn) {
            INITCOMMONCONTROLSEX ic;
            ic.dwSize = sizeof(ic);
            ic.dwICC = ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES | ICC_BAR_CLASSES | ICC_UPDOWN_CLASS | ICC_PROGRESS_CLASS;
            fn(&ic);
        }
    }

    GetModuleFileNameA(0, g.exeDir, sizeof(g.exeDir));
    p = g.exeDir;
    for (i = 0; g.exeDir[i]; i++) if (g.exeDir[i] == '\\') p = g.exeDir + i;
    *p = 0;
    wsprintfA(g.iniPath, "%s\\NEOFAN.INI", g.exeDir);

    hw_os(&g.hw.os);
    g.hw.demo = has_arg(cmd, "/demo") || has_arg(cmd, "-demo");
    g.hw.ioAllowed = !g.hw.os.isNT && !g.hw.demo && !has_arg(cmd, "/nohw");

    app_init_model();
    app_load_settings();

    g.font = dialog_font();
    GetObjectA(g.font, sizeof(lf), &lf);
    lf.lfWeight = FW_BOLD;
    g.bold = CreateFontIndirectA(&lf);
    g.ledFont = CreateFontA(-30, 0, 0, 0, FW_BOLD, 0, 0, 0, ANSI_CHARSET, 0, 0, DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, "Courier New");
    g.smallFont = CreateFontA(-12, 0, 0, 0, FW_BOLD, 0, 0, 0, ANSI_CHARSET, 0, 0, DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, "Courier New");
    g.swatches = make_swatches();
    draw_register(g.inst);

    ZERO(wc);
    wc.lpfnWndProc = main_proc;
    wc.hInstance = g.inst;
    wc.hIcon = LoadIconA(g.inst, MAKEINTRESOURCEA(IDI_APP));
    wc.hCursor = LoadCursor(0, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszMenuName = MAKEINTRESOURCEA(IDR_MENU);
    wc.lpszClassName = "neoFanSpeedMain";
    RegisterClassA(&wc);
    ZERO(wc);
    wc.lpfnWndProc = mini_proc;
    wc.hInstance = g.inst;
    wc.hIcon = LoadIconA(g.inst, MAKEINTRESOURCEA(IDI_APP));
    wc.hCursor = LoadCursor(0, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = "neoFanSpeedMini";
    RegisterClassA(&wc);

    g.taskbarMsg = RegisterWindowMessageA("TaskbarCreated");
    g.accel = LoadAcceleratorsA(g.inst, MAKEINTRESOURCEA(IDR_ACCEL));

    if (!CreateWindowExA(0, "neoFanSpeedMain", g.hw.demo ? APP_NAME " (demo data)" : APP_NAME,
                         WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
                         CW_USEDEFAULT, CW_USEDEFAULT, 760, 600, 0, 0, g.inst, 0))
        return 1;

    ui_layout();
    ui_set_tab(g.curTab);

    if (g.firstRun) {
        ShowWindow(g.main, SW_SHOW);
        UpdateWindow(g.main);
        ui_detect(g.main);
        app_save_settings();
    } else {
        HCURSOR oc = SetCursor(LoadCursor(0, IDC_WAIT));
        hw_cpu(&g.hw.cpu);
        hw_gpu(&g.hw.gpu);
        hw_board(&g.hw);
        hw_chip_detect(&g.hw);
        hw_smart_scan(&g.hw);
        SetCursor(oc);
        apply_detection();
    }
    if (g.onTop) { g.onTop = 0; on_command(IDM_ONTOP, 0, 0); }
    if (g.miniView) on_command(IDM_MINI, 0, 0);
    else if (!g.firstRun) ShowWindow(g.main, SW_SHOW);
    ui_tray_update();
    if (g.log.onLaunch) log_toggle();
    SetTimer(g.main, TIMER_ID, 1000, 0);
    ui_refresh();

    while (GetMessageA(&msg, 0, 0, 0) > 0) {
        if (g.alarmDlg && IsDialogMessageA(g.alarmDlg, &msg)) continue;
        if (msg.hwnd == g.main || IsChild(g.main, msg.hwnd))
            if (TranslateAcceleratorA(g.main, g.accel, &msg)) continue;
        if (g.page[g.curTab] && IsDialogMessageA(g.page[g.curTab], &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    if (once) CloseHandle(once);
    return (int)msg.wParam;
}

void WinMainCRTStartup(void)
{
    ExitProcess((UINT)nfs_main());
}
