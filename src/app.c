/* Sensor and fan model, fan speed rules, settings file */
#include "rt.h"
#include "app.h"

App g;

static const int g_pmap[3] = { 40, 65, 100 };

static void sens(int i, const char *name, const char *sh, COLORREF c, int dash, double limit)
{
    rt_cpy(g.s[i].name, name, sizeof(g.s[i].name));
    rt_cpy(g.s[i].shortName, sh, sizeof(g.s[i].shortName));
    g.s[i].color = c;
    g.s[i].dash = dash;
    g.s[i].limit = limit;
}

void app_init_model(void)
{
    int i;
    sens(S_CPU, "CPU Diode", "CPU", RGB(0, 255, 0), PS_SOLID, 60);
    sens(S_BOARD, "Motherboard", "Board", RGB(0, 255, 255), PS_SOLID, 50);
    sens(S_AUX, "Aux Sensor", "Aux", RGB(255, 255, 0), PS_DOT, 55);
    sens(S_GPU, "GPU Card", "GPU", RGB(255, 80, 80), PS_DASH, 80);
    sens(S_HDD, "HDD 0 (S.M.A.R.T.)", "HDD", RGB(255, 128, 255), PS_DOT, 50);
    g.s[S_CPU].plot = g.s[S_BOARD].plot = g.s[S_GPU].plot = 1;

    for (i = 0; i < NFAN; i++) {
        Fan *f = &g.f[i];
        wsprintfA(f->name, "Fan %d", i + 1);
        wsprintfA(f->shortName, "Fan %d", i + 1);
        wsprintfA(f->header, "Chip fan input %d", i + 1);
        f->mode = M_AUTO;
        f->pct = 65;
        f->profile = P_NORMAL;
        f->follows = S_CPU;
        f->pts[0][0] = 35; f->pts[0][1] = 35;
        f->pts[1][0] = 48; f->pts[1][1] = 50;
        f->pts[2][0] = 56; f->pts[2][1] = 75;
        f->pts[3][0] = 62; f->pts[3][1] = 100;
        f->alarm = 1000;
        f->allowBelow = -1;
        f->hT = -1000;
        f->lastWritten = -2;
    }
    rt_cpy(g.f[0].name, "CPU Fan", sizeof(g.f[0].name));
    rt_cpy(g.f[0].shortName, "CPU fan", sizeof(g.f[0].shortName));
    rt_cpy(g.f[1].name, "System Fan", sizeof(g.f[1].name));
    rt_cpy(g.f[1].shortName, "Sys fan", sizeof(g.f[1].shortName));
    rt_cpy(g.f[2].name, "Chassis Fan", sizeof(g.f[2].name));
    rt_cpy(g.f[2].shortName, "Case fan", sizeof(g.f[2].shortName));
    g.f[1].follows = S_BOARD;
    g.f[1].pts[0][0] = 30; g.f[1].pts[0][1] = 30;
    g.f[1].pts[1][0] = 38; g.f[1].pts[1][1] = 45;
    g.f[1].pts[2][0] = 45; g.f[1].pts[2][1] = 70;
    g.f[1].pts[3][0] = 50; g.f[1].pts[3][1] = 100;
    g.f[0].alarmOn = 1;
    g.f[0].alarm = 1500;

    g.unit = 'C';
    g.o.bios = 1;
    g.o.failsafe = 1;
    g.o.floor = 30;
    g.o.warnWin = 1;
    g.o.beep = 0;
    g.o.balloon = 1;
    g.o.hyst = 3;
    g.o.applyAll = 1;

    rt_cpy(g.log.path, "C:\\NEOFAN\\NFS001.CSV", sizeof(g.log.path));
    g.log.csv = 1;
    g.log.interval = 2;
    for (i = 0; i < NSENS; i++) g.log.incS[i] = 1;
    for (i = 0; i < NFAN; i++) { g.log.incF[i] = 1; g.log.incD[i] = 0; }
    g.log.header = 1;
    g.log.roll = 1;
    g.selSensor = S_CPU;
    g.selFan = 0;
    g.alarmSensor = -1;
}

double app_cv(double c) { return g.unit == 'F' ? c * 1.8 + 32 : c; }

char *app_ft(char *buf, double c)
{
    char n[24];
    rt_fmt1(n, app_cv(c));
    wsprintfA(buf, "%s \xB0%c", n, g.unit);
    return buf;
}

double app_temp_of(int id)
{
    if (id == S_HOT) {
        double t = -100;
        if (g.s[S_CPU].avail) t = g.s[S_CPU].val;
        if (g.s[S_GPU].avail && g.s[S_GPU].val > t) t = g.s[S_GPU].val;
        if (t < -99) t = g.s[S_BOARD].val;
        return t;
    }
    if (id < 0 || id >= NSENS) return 0;
    return g.s[id].val;
}

static double interp(int pts[NPTS][2], double t)
{
    int i;
    if (t <= pts[0][0]) return pts[0][1];
    for (i = 1; i < NPTS; i++) {
        if (t <= pts[i][0]) {
            double a = pts[i - 1][0], b = pts[i - 1][1], c = pts[i][0], d = pts[i][1];
            if (c <= a) return d;
            return b + (d - b) * (t - a) / (c - a);
        }
    }
    return pts[NPTS - 1][1];
}

double app_duty_of(Fan *f)
{
    double t = app_temp_of(f->follows), lo;
    int fl = g.o.floor;
    if (!f->ctrl) return -1;
    /* hysteresis: falling temperature is held for hyst degrees */
    if (f->hT < -999) f->hT = t;
    lo = t;
    if (f->hT > t + g.o.hyst) f->hT = t + g.o.hyst;
    if (f->hT < lo) f->hT = lo;
    if (f->mode == M_AUTO) return -1;          /* the BIOS decides */
    if (f->mode == M_MANUAL) {
        if (f->allowBelow >= 0 && f->pct >= f->allowBelow) return f->pct;
        return f->pct < fl ? fl : f->pct;
    }
    if (f->mode == M_PROFILE) {
        int p = g_pmap[f->profile];
        return p < fl ? fl : p;
    }
    {
        double d = interp(f->pts, f->hT);
        return d < fl ? fl : d;
    }
}

int app_count_ctrl(void)
{
    int i, n = 0;
    for (i = 0; i < NFAN; i++) if (g.f[i].ctrl) n++;
    return n;
}

void app_set_all(int p)
{
    int i;
    for (i = 0; i < NFAN; i++) {
        if (!g.f[i].ctrl) continue;
        if (p < 0) g.f[i].mode = M_AUTO;
        else { g.f[i].mode = M_PROFILE; g.f[i].profile = p; }
    }
    g.fsActive = 0;
    app_apply_hw();
}

int app_cur_profile(void)
{
    int i, first = -1, allAuto = 1, same = 1, p = -1;
    for (i = 0; i < NFAN; i++) {
        if (!g.f[i].ctrl) continue;
        if (first < 0) { first = i; p = g.f[i].profile; }
        if (g.f[i].mode != M_AUTO) allAuto = 0;
        if (g.f[i].mode != M_PROFILE || g.f[i].profile != p) same = 0;
    }
    if (first < 0) return -3;
    if (allAuto) return -1;
    if (same) return p;
    return -2;
}

void app_apply_hw(void)
{
    int i;
    for (i = 0; i < NFAN; i++) {
        Fan *f = &g.f[i];
        if (!f->ctrl) continue;
        f->duty = app_duty_of(f);
        if (f->mode == M_AUTO) {
            if (f->lastWritten != -1) { hw_chip_pwm(&g.hw, i, -1); f->lastWritten = -1; }
        } else {
            int d = rt_round(f->duty);
            if (d != f->lastWritten) { hw_chip_pwm(&g.hw, i, d); f->lastWritten = d; }
        }
    }
}

void app_reset_stats(void)
{
    int i;
    for (i = 0; i < NSENS; i++) {
        Sensor *s = &g.s[i];
        s->min = s->max = s->sum = s->val;
        s->n = 1;
    }
}

static void stamp_hm(char *out)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    wsprintfA(out, "%02d:%02d", st.wHour, st.wMinute);
}

void app_force_full(int sensor)
{
    int i;
    if (!g.fsActive) {
        for (i = 0; i < NFAN; i++) { g.prevMode[i] = g.f[i].mode; g.prevProfile[i] = g.f[i].profile; }
    }
    for (i = 0; i < NFAN; i++) {
        if (!g.f[i].ctrl) continue;
        g.f[i].mode = M_PROFILE;
        g.f[i].profile = P_FULL;
    }
    g.fsActive = 1;
    if (sensor >= 0) {
        rt_cpy(g.fsName, g.s[sensor].name, sizeof(g.fsName));
        app_ft(g.fsLim, g.s[sensor].limit);
        stamp_hm(g.fsTime);
    }
    app_apply_hw();
}

void app_restore_prev(void)
{
    int i;
    if (g.fsActive) {
        for (i = 0; i < NFAN; i++) { g.f[i].mode = g.prevMode[i]; g.f[i].profile = g.prevProfile[i]; }
    }
    g.fsActive = 0;
    app_apply_hw();
}

static void trip(int id)
{
    Sensor *s = &g.s[id];
    int fs = g.o.failsafe && app_count_ctrl() > 0;
    char t[24], msg[200];
    if (fs) app_force_full(id);
    if (g.o.beep) MessageBeep(MB_ICONEXCLAMATION);
    g.alarmTemp = s->val;
    if (g.o.warnWin) ui_show_alarm(id);
    if (g.o.balloon && g.hw.os.hasBalloon) {
        wsprintfA(msg, "%s is at %s.%s", s->name, app_ft(t, s->val), fs ? " Fans set to full speed." : "");
        ui_balloon("Temperature alarm", msg);
    }
}

static void push_sensor(int i, int ok, double v)
{
    Sensor *s = &g.s[i];
    int k;
    s->avail = ok;
    if (!ok) return;
    s->val = v;
    if (s->nhist < HIST) s->hist[s->nhist++] = v;
    else {
        for (k = 1; k < HIST; k++) s->hist[k - 1] = s->hist[k];
        s->hist[HIST - 1] = v;
    }
    if (s->n == 0) { s->min = s->max = v; s->sum = 0; }
    if (v < s->min) s->min = v;
    if (v > s->max) s->max = v;
    s->sum += v;
    s->n++;
}

void app_tick(int fromTimer)
{
    ChipReading *r = &g.rd;
    int i, got;
    double hdd = 0;
    static int hddOk = 0;
    g.ticks++;
    got = hw_chip_read(&g.hw, r);
    if (got) {
        /* temp 2 is the CPU diode on most boards, temp 1 the board */
        push_sensor(S_CPU, g.hw.chip.hasTemp[1] && r->tempOk[1], r->temp[1]);
        push_sensor(S_BOARD, g.hw.chip.hasTemp[0] && r->tempOk[0], r->temp[0]);
        push_sensor(S_AUX, g.hw.chip.hasTemp[2] && r->tempOk[2], r->temp[2]);
    } else {
        push_sensor(S_CPU, 0, 0);
        push_sensor(S_BOARD, 0, 0);
        push_sensor(S_AUX, 0, 0);
    }
    if (g.hw.demo) {
        static double gpu = 57;
        gpu += (57 + 3 * ((g.ticks / 9) % 2 ? 1 : -1) - gpu) * 0.1 + (rt_rand() - 0.5) * 0.4;
        push_sensor(S_GPU, 1, gpu);
    } else {
        push_sensor(S_GPU, 0, 0);
    }
    if (g.hw.smartDrive >= 0 && (g.ticks % 5 == 1 || !hddOk || g.hw.demo)) {
        hddOk = hw_smart_temp(&g.hw, &hdd);
        if (hddOk) g.s[S_HDD].val = hdd;
    }
    push_sensor(S_HDD, hddOk && g.hw.smartDrive >= 0, g.s[S_HDD].val);

    for (i = 0; i < NFAN; i++) {
        Fan *f = &g.f[i];
        f->avail = got && g.hw.chip.hasFan[i] && r->rpmOk[i];
        f->rpm = f->avail ? r->rpm[i] : 0;
        if (f->rpm > f->maxRpm) f->maxRpm = f->rpm;
    }
    app_apply_hw();
    for (i = 0; i < NFAN; i++) {
        Fan *f = &g.f[i];
        if (f->ctrl && f->maxRpm > 0 && f->duty >= 0) {
            int full = f->maxRpm;
            if (f->lastWritten > 0 && f->lastWritten < 100 && f->rpm > 0) {
                int est = f->rpm * 100 / f->lastWritten;
                if (est > full) full = est;
            }
            f->target = rt_round(full * f->duty / 100.0);
        } else {
            f->target = f->rpm;
        }
    }

    if (fromTimer && g.ticks > 2) {
        for (i = 0; i < NSENS; i++) {
            Sensor *s = &g.s[i];
            if (!s->avail) { s->alerted = 0; continue; }
            if (s->val > s->limit && !s->alerted) { s->alerted = 1; trip(i); }
            if (s->val < s->limit - 2) s->alerted = 0;
        }
    }
    if (fromTimer) log_tick();
}

/* ------------------------------------------------------------------ */
/* settings in NEOFAN.INI next to the program                          */

static int ini_int(const char *sec, const char *key, int def)
{
    return (int)GetPrivateProfileIntA(sec, key, def, g.iniPath);
}

static void ini_put(const char *sec, const char *key, int v)
{
    char b[16];
    wsprintfA(b, "%d", v);
    WritePrivateProfileStringA(sec, key, b, g.iniPath);
}

void app_load_settings(void)
{
    int i, k;
    char key[32], buf[MAX_PATH];
    g.firstRun = ini_int("General", "Detected", 0) == 0;
    g.unit = ini_int("General", "Fahrenheit", 0) ? 'F' : 'C';
    g.onTop = ini_int("General", "OnTop", 0);
    g.miniView = ini_int("General", "MiniView", 0);
    g.curTab = ini_int("General", "Tab", 0) & 3;
    g.o.bios = ini_int("Safety", "BiosOnExit", 1);
    g.o.failsafe = ini_int("Safety", "FullOnAlarm", 1);
    g.o.floor = ini_int("Safety", "LowestDuty", 30);
    if (g.o.floor < 20 || g.o.floor > 50) g.o.floor = 30;
    g.o.warnWin = ini_int("Alarm", "Window", 1);
    g.o.beep = ini_int("Alarm", "Beep", 0);
    g.o.balloon = ini_int("Alarm", "Balloon", 1);
    g.o.hyst = ini_int("Fans", "Hysteresis", 3);
    g.o.applyAll = ini_int("Fans", "ApplyAll", 1);
    for (i = 0; i < NSENS; i++) {
        wsprintfA(key, "Sensor%d", i + 1);
        GetPrivateProfileStringA(key, "Name", g.s[i].name, buf, 40, g.iniPath);
        rt_cpy(g.s[i].name, buf, sizeof(g.s[i].name));
        g.s[i].limit = ini_int(key, "Limit", (int)g.s[i].limit);
        g.s[i].plot = ini_int(key, "Plot", g.s[i].plot);
    }
    for (i = 0; i < NFAN; i++) {
        Fan *f = &g.f[i];
        wsprintfA(key, "Fan%d", i + 1);
        GetPrivateProfileStringA(key, "Name", f->name, buf, 40, g.iniPath);
        rt_cpy(f->name, buf, sizeof(f->name));
        f->mode = ini_int(key, "Mode", f->mode) & 3;
        f->pct = ini_int(key, "Duty", f->pct);
        if (f->pct < 20 || f->pct > 100) f->pct = 65;
        f->profile = ini_int(key, "Profile", f->profile) % 3;
        f->follows = ini_int(key, "Follows", f->follows);
        if (f->follows != S_HOT && (f->follows < 0 || f->follows >= NSENS)) f->follows = S_CPU;
        f->alarmOn = ini_int(key, "WarnOn", f->alarmOn);
        f->alarm = ini_int(key, "WarnRpm", f->alarm);
        for (k = 0; k < NPTS; k++) {
            char pk[8];
            wsprintfA(pk, "T%d", k + 1);
            f->pts[k][0] = ini_int(key, pk, f->pts[k][0]);
            wsprintfA(pk, "D%d", k + 1);
            f->pts[k][1] = ini_int(key, pk, f->pts[k][1]);
        }
    }
    GetPrivateProfileStringA("Log", "File", g.log.path, buf, MAX_PATH, g.iniPath);
    rt_cpy(g.log.path, buf, sizeof(g.log.path));
    g.log.csv = ini_int("Log", "Csv", 1);
    g.log.interval = ini_int("Log", "Every", 2);
    if (g.log.interval < 1 || g.log.interval > 3600) g.log.interval = 2;
    g.log.header = ini_int("Log", "Header", 1);
    g.log.roll = ini_int("Log", "NewFileAt1MB", 1);
    g.log.onLaunch = ini_int("Log", "OnStart", 0);
    k = ini_int("Log", "Columns", -1);
    if (k >= 0) {
        for (i = 0; i < NSENS; i++) g.log.incS[i] = (k >> i) & 1;
        for (i = 0; i < NFAN; i++) g.log.incF[i] = (k >> (8 + i)) & 1;
        for (i = 0; i < NFAN; i++) g.log.incD[i] = (k >> (16 + i)) & 1;
    }
}

void app_save_settings(void)
{
    int i, k, cols = 0;
    char key[32];
    ini_put("General", "Detected", 1);
    ini_put("General", "Fahrenheit", g.unit == 'F');
    ini_put("General", "OnTop", g.onTop);
    ini_put("General", "MiniView", g.miniView);
    ini_put("General", "Tab", g.curTab);
    ini_put("Safety", "BiosOnExit", g.o.bios);
    ini_put("Safety", "FullOnAlarm", g.o.failsafe);
    ini_put("Safety", "LowestDuty", g.o.floor);
    ini_put("Alarm", "Window", g.o.warnWin);
    ini_put("Alarm", "Beep", g.o.beep);
    ini_put("Alarm", "Balloon", g.o.balloon);
    ini_put("Fans", "Hysteresis", g.o.hyst);
    ini_put("Fans", "ApplyAll", g.o.applyAll);
    for (i = 0; i < NSENS; i++) {
        wsprintfA(key, "Sensor%d", i + 1);
        WritePrivateProfileStringA(key, "Name", g.s[i].name, g.iniPath);
        ini_put(key, "Limit", (int)g.s[i].limit);
        ini_put(key, "Plot", g.s[i].plot);
    }
    for (i = 0; i < NFAN; i++) {
        Fan *f = &g.f[i];
        int mode = f->mode, prof = f->profile;
        if (g.fsActive) { mode = g.prevMode[i]; prof = g.prevProfile[i]; }
        wsprintfA(key, "Fan%d", i + 1);
        WritePrivateProfileStringA(key, "Name", f->name, g.iniPath);
        ini_put(key, "Mode", mode);
        ini_put(key, "Duty", f->pct);
        ini_put(key, "Profile", prof);
        ini_put(key, "Follows", f->follows);
        ini_put(key, "WarnOn", f->alarmOn);
        ini_put(key, "WarnRpm", f->alarm);
        for (k = 0; k < NPTS; k++) {
            char pk[8];
            wsprintfA(pk, "T%d", k + 1);
            ini_put(key, pk, f->pts[k][0]);
            wsprintfA(pk, "D%d", k + 1);
            ini_put(key, pk, f->pts[k][1]);
        }
    }
    WritePrivateProfileStringA("Log", "File", g.log.path, g.iniPath);
    ini_put("Log", "Csv", g.log.csv);
    ini_put("Log", "Every", g.log.interval);
    ini_put("Log", "Header", g.log.header);
    ini_put("Log", "NewFileAt1MB", g.log.roll);
    ini_put("Log", "OnStart", g.log.onLaunch);
    for (i = 0; i < NSENS; i++) cols |= g.log.incS[i] << i;
    for (i = 0; i < NFAN; i++) cols |= g.log.incF[i] << (8 + i);
    for (i = 0; i < NFAN; i++) cols |= g.log.incD[i] << (16 + i);
    ini_put("Log", "Columns", cols);
}
