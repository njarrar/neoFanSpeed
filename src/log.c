/* Log file: CSV or plain text, one line every few seconds.
   Names stay 8.3 (NFS001.CSV) so they work on FAT, FAT32 and NTFS. */
#include "rt.h"
#include "app.h"

#define ROLL_AT   (1024L * 1024L)
#define HARD_CAP  2000000000UL      /* stay well under the FAT32 4 GB file limit */

static double logcv(double c)
{
    return g.log.unitAtStart == 'F' ? c * 1.8 + 32 : c;
}

static char logunit(void)
{
    return g.log.on ? g.log.unitAtStart : g.unit;
}

void log_header(char *out, int cap)
{
    int i;
    char col[64];
    rt_cpy(out, "Time", cap);
    for (i = 0; i < NSENS; i++) {
        if (!g.log.incS[i] || !g.s[i].avail) continue;
        wsprintfA(col, ",%s (%c)", g.s[i].name, logunit());
        rt_cat(out, col, cap);
    }
    for (i = 0; i < NFAN; i++) {
        if (g.f[i].avail && g.log.incF[i]) {
            wsprintfA(col, ",%s (RPM)", g.f[i].name);
            rt_cat(out, col, cap);
        }
        if (g.f[i].ctrl && g.log.incD[i]) {
            wsprintfA(col, ",%s duty (%%)", g.f[i].name);
            rt_cat(out, col, cap);
        }
    }
}

void log_line(char *out, int cap, int forDisplay)
{
    SYSTEMTIME st;
    int i;
    char v[32], col[64];
    char u = logunit();
    (void)forDisplay;
    GetLocalTime(&st);
    wsprintfA(out, "%04d-%02d-%02d %02d:%02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    for (i = 0; i < NSENS; i++) {
        double t;
        if (!g.log.incS[i] || !g.s[i].avail) continue;
        t = g.log.on ? logcv(g.s[i].val) : app_cv(g.s[i].val);
        rt_fmt1(v, t);
        if (g.log.csv) wsprintfA(col, ",%s", v);
        else wsprintfA(col, "  %s %s%c", g.s[i].shortName, v, u);
        rt_cat(out, col, cap);
    }
    for (i = 0; i < NFAN; i++) {
        Fan *f = &g.f[i];
        if (f->avail && g.log.incF[i]) {
            if (g.log.csv) wsprintfA(col, f->under ? ",<%d" : ",%d", f->under ? f->under : f->rpm);
            else wsprintfA(col, f->under ? "  %s <%drpm" : "  %s %drpm", f->shortName, f->under ? f->under : f->rpm);
            rt_cat(out, col, cap);
        }
        if (f->ctrl && g.log.incD[i]) {
            int d = f->duty < 0 ? -1 : rt_round(f->duty);
            if (g.log.csv) {
                if (d < 0) wsprintfA(col, ",BIOS");
                else wsprintfA(col, ",%d", d);
            } else {
                if (d < 0) wsprintfA(col, "  %s duty BIOS", f->shortName);
                else wsprintfA(col, "  %s duty %d%%", f->shortName, d);
            }
            rt_cat(out, col, cap);
        }
    }
}

static void make_dirs(const char *path)
{
    char d[MAX_PATH];
    int i, n;
    rt_cpy(d, path, sizeof(d));
    n = rt_len(d);
    for (i = 3; i < n; i++) {
        if (d[i] == '\\') {
            d[i] = 0;
            CreateDirectoryA(d, 0);
            d[i] = '\\';
        }
    }
}

/* NFS001.CSV -> NFS002.CSV; returns 0 when the name has no number or
   the number would not fit (NFS999.CSV), so logging stays on that file */
static int next_name(char *path)
{
    int n = rt_len(path), dot = n, i, end, start;
    long num, lim = 1;
    for (i = n - 1; i >= 0 && path[i] != '\\'; i--)
        if (path[i] == '.') { dot = i; break; }
    end = dot;
    start = end;
    /* at most 9 digits, so the number fits in a long */
    while (start > 0 && end - start < 9 && path[start - 1] >= '0' && path[start - 1] <= '9') start--;
    if (end - start < 1) return 0;
    num = 0;
    for (i = start; i < end; i++) num = num * 10 + (path[i] - '0');
    num++;
    for (i = start; i < end; i++) lim *= 10;
    if (num >= lim) return 0;
    for (i = end - 1; i >= start; i--) { path[i] = (char)('0' + num % 10); num /= 10; }
    return 1;
}

static int append(const char *line)
{
    HANDLE h;
    DWORD w = 0, sz;
    char buf[1100];
    int n;
    rt_cpy(buf, line, sizeof(buf) - 2);
    rt_cat(buf, "\r\n", sizeof(buf));
    n = rt_len(buf);
    h = CreateFileA(g.log.path, GENERIC_WRITE, FILE_SHARE_READ, 0, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (h == INVALID_HANDLE_VALUE) {
        make_dirs(g.log.path);
        h = CreateFileA(g.log.path, GENERIC_WRITE, FILE_SHARE_READ, 0, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    }
    if (h == INVALID_HANDLE_VALUE) {
        wsprintfA(g.log.error, "Could not open %s (error %lu).", g.log.path, GetLastError());
        return 0;
    }
    sz = GetFileSize(h, 0);
    if (sz != 0xFFFFFFFF && sz > HARD_CAP) {
        CloseHandle(h);
        wsprintfA(g.log.error, "%s is full (2 GB). Pick a new file to keep logging.", g.log.path);
        return 0;
    }
    SetFilePointer(h, 0, 0, FILE_END);
    if (!WriteFile(h, buf, (DWORD)n, &w, 0) || w != (DWORD)n) {
        CloseHandle(h);
        wsprintfA(g.log.error, "Could not write to %s. Is the disk full?", g.log.path);
        return 0;
    }
    CloseHandle(h);
    g.log.bytes = (sz == 0xFFFFFFFF ? 0 : sz) + w;
    g.log.error[0] = 0;
    return 1;
}

static void remember(const char *line)
{
    int i;
    if (g.log.nrecent == 9) {
        for (i = 1; i < 9; i++) rt_cpy(g.log.recent[i - 1], g.log.recent[i], 200);
        g.log.nrecent--;
    }
    rt_cpy(g.log.recent[g.log.nrecent++], line, 200);
}

static int write_one(void)
{
    char line[1024];
    DWORD sz = 0;
    HANDLE h;
    if (g.log.roll) {
        h = CreateFileA(g.log.path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, 0, 0);
        if (h != INVALID_HANDLE_VALUE) { sz = GetFileSize(h, 0); CloseHandle(h); }
        if (sz != 0xFFFFFFFF && sz >= ROLL_AT && next_name(g.log.path)) {
            if (g.log.csv && g.log.header) {
                log_header(line, sizeof(line));
                if (!append(line)) return 0;
            }
        }
    }
    log_line(line, sizeof(line), 0);
    if (!append(line)) return 0;
    remember(line);
    g.log.rows++;
    return 1;
}

void log_toggle(void)
{
    if (g.log.on) {
        g.log.on = 0;
        return;
    }
    g.log.on = 1;
    g.log.tick = 0;
    g.log.unitAtStart = g.unit;
    g.log.error[0] = 0;
    if (g.log.csv && g.log.header) {
        HANDLE h = CreateFileA(g.log.path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, 0, 0);
        DWORD sz = 0;
        if (h != INVALID_HANDLE_VALUE) { sz = GetFileSize(h, 0); CloseHandle(h); }
        if (h == INVALID_HANDLE_VALUE || sz == 0) {
            char line[1024];
            log_header(line, sizeof(line));
            if (!append(line)) { g.log.on = 0; return; }
            remember(line);
        }
    }
    if (!write_one()) g.log.on = 0;
}

void log_tick(void)
{
    if (!g.log.on) return;
    g.log.tick++;
    if (g.log.tick % g.log.interval == 0)
        if (!write_one()) g.log.on = 0;
}
