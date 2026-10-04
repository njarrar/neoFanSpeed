/* Hardware access for neoFanSpeed.

   Windows 98, 98SE and ME let a normal program use the IN and OUT
   instructions on the sensor chip ports, so the chip is read directly.
   Windows 2000 and XP block those instructions for programs, so there the
   chip is left alone and only what Windows itself reports is shown. */
#include "rt.h"
#include "hw.h"

/* ------------------------------------------------------------------ */
/* port access, used only when hw->ioAllowed is set (Windows 9x)      */

static BYTE inb(WORD port)
{
    BYTE v;
    __asm__ __volatile__("inb %w1, %b0" : "=a"(v) : "Nd"(port));
    return v;
}

static void outb(WORD port, BYTE v)
{
    __asm__ __volatile__("outb %b0, %w1" : : "a"(v), "Nd"(port));
}

/* ------------------------------------------------------------------ */
/* registry helpers                                                    */

static int reg_get(HKEY root, const char *path, const char *val, char *out, int cap, DWORD *type)
{
    HKEY k;
    DWORD sz = (DWORD)cap, t = 0;
    out[0] = 0;
    if (RegOpenKeyExA(root, path, 0, KEY_READ, &k) != ERROR_SUCCESS) return 0;
    if (RegQueryValueExA(k, val, 0, &t, (BYTE *)out, &sz) != ERROR_SUCCESS) { RegCloseKey(k); out[0] = 0; return 0; }
    RegCloseKey(k);
    if (sz >= (DWORD)cap) sz = cap - 1;
    out[sz] = 0;
    if (type) *type = t;
    return (int)sz;
}

static int reg_str(HKEY root, const char *path, const char *val, char *out, int cap)
{
    DWORD t;
    int n = reg_get(root, path, val, out, cap, &t);
    if (!n) return 0;
    if (t == REG_BINARY && n >= 4 && out[1] == 0) {
        /* UTF-16 text stored as binary: keep the low bytes */
        int i, j = 0;
        for (i = 0; i + 1 < n && out[i]; i += 2) out[j++] = out[i];
        out[j] = 0;
    }
    if (t != REG_SZ && t != REG_MULTI_SZ && t != REG_EXPAND_SZ && t != REG_BINARY) { out[0] = 0; return 0; }
    rt_cpy(out, rt_trim(out), cap);
    return out[0] != 0;
}

/* ------------------------------------------------------------------ */
/* Windows version                                                     */

typedef struct { DWORD cbSize, dwMajorVersion, dwMinorVersion, dwBuildNumber, dwPlatformID; } NFS_DLLVERSIONINFO;
typedef HRESULT (CALLBACK *DLLGETVERSIONPROC_)(NFS_DLLVERSIONINFO *);
typedef BOOL (WINAPI *CHECKTOKENMEMBERSHIP_)(HANDLE, PSID, PBOOL);
typedef BOOL (WINAPI *ALLOCSID_)(PSID_IDENTIFIER_AUTHORITY, BYTE, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, PSID *);
typedef PVOID (WINAPI *FREESID_)(PSID);

static int shell_major(void)
{
    HMODULE h = GetModuleHandleA("SHELL32.DLL");
    DLLGETVERSIONPROC_ fn;
    NFS_DLLVERSIONINFO v;
    if (!h) h = LoadLibraryA("SHELL32.DLL");
    if (!h) return 4;
    fn = (DLLGETVERSIONPROC_)GetProcAddress(h, "DllGetVersion");
    if (!fn) return 4;
    ZERO(v);
    v.cbSize = sizeof(v);
    if (fn(&v) != S_OK) return 4;
    return (int)v.dwMajorVersion;
}

static int nt_is_admin(void)
{
    HMODULE h = LoadLibraryA("ADVAPI32.DLL");
    CHECKTOKENMEMBERSHIP_ check;
    ALLOCSID_ alloc;
    FREESID_ freesid;
    SID_IDENTIFIER_AUTHORITY nt = { SECURITY_NT_AUTHORITY };
    PSID sid = 0;
    BOOL member = FALSE;
    if (!h) return 0;
    check = (CHECKTOKENMEMBERSHIP_)GetProcAddress(h, "CheckTokenMembership");
    alloc = (ALLOCSID_)GetProcAddress(h, "AllocateAndInitializeSid");
    freesid = (FREESID_)GetProcAddress(h, "FreeSid");
    if (!check || !alloc || !freesid) return 0;
    if (!alloc(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &sid)) return 0;
    if (!check(0, sid, &member)) member = FALSE;
    freesid(sid);
    return member ? 1 : 0;
}

void hw_os(OsInfo *os)
{
    OSVERSIONINFOA v;
    char sp[128];
    ZERO(*os);
    ZERO(v);
    v.dwOSVersionInfoSize = sizeof(v);
    GetVersionExA(&v);
    os->major = (int)v.dwMajorVersion;
    os->minor = (int)v.dwMinorVersion;
    os->build = (int)(v.dwBuildNumber & 0xFFFF);
    os->isNT = v.dwPlatformId == VER_PLATFORM_WIN32_NT;
    rt_cpy(sp, rt_trim(v.szCSDVersion), sizeof(sp));
    if (!os->isNT) {
        if (os->major == 4 && os->minor >= 90) {
            rt_cpy(os->shortName, "ME", 16);
            wsprintfA(os->name, "Millennium Edition (4.90.%d)", os->build);
        } else if (os->major == 4 && os->minor >= 10) {
            if (os->build >= 2222) {
                rt_cpy(os->shortName, "98SE", 16);
                wsprintfA(os->name, "98 Second Edition (4.10.%d%s%s)", os->build, sp[0] ? " " : "", sp);
            } else {
                rt_cpy(os->shortName, "98", 16);
                wsprintfA(os->name, "98 (4.10.%d%s%s)", os->build, sp[0] ? " " : "", sp);
            }
        } else {
            rt_cpy(os->shortName, "95", 16);
            wsprintfA(os->name, "95 (4.%02d.%d)", os->minor, os->build);
        }
        os->isAdmin = 1;
    } else {
        if (os->major == 5 && os->minor == 0) rt_cpy(os->shortName, "2000", 16);
        else if (os->major == 5) rt_cpy(os->shortName, "XP", 16);
        else if (os->major == 4) rt_cpy(os->shortName, "NT 4.0", 16);
        else rt_cpy(os->shortName, "NT", 16);
        wsprintfA(os->name, "%s%s%s (%d.%d.%d)", os->shortName, sp[0] ? " " : "", sp, os->major, os->minor, os->build);
        os->isAdmin = nt_is_admin();
    }
    /* balloon tips came with shell32 5.0: Windows ME, 2000 and XP */
    os->hasBalloon = shell_major() >= 5 && (os->isNT || os->minor >= 90);
}

/* ------------------------------------------------------------------ */
/* CPU                                                                 */

static int has_cpuid(void)
{
    DWORD a, b;
    __asm__ __volatile__(
        "pushfl\n\t"
        "popl %0\n\t"
        "movl %0, %1\n\t"
        "xorl $0x200000, %0\n\t"
        "pushl %0\n\t"
        "popfl\n\t"
        "pushfl\n\t"
        "popl %0\n\t"
        "pushl %1\n\t"
        "popfl"
        : "=&r"(a), "=&r"(b));
    return ((a ^ b) & 0x200000) != 0;
}

static void cpuid(DWORD leaf, DWORD r[4])
{
    __asm__ __volatile__("cpuid" : "=a"(r[0]), "=b"(r[1]), "=c"(r[2]), "=d"(r[3]) : "a"(leaf), "c"(0));
}

static double rdtsc(void)
{
    DWORD lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return (double)hi * 4294967296.0 + (double)lo;
}

static const char *amd_core(int fam, int model)
{
    if (fam == 5) {
        if (model <= 3) return "K5";
        if (model <= 7) return "K6";
        if (model == 8) return "K6-2";
        if (model == 9) return "K6-III";
        if (model == 13) return "K6-2+ / K6-III+";
    }
    if (fam == 6) {
        switch (model) {
        case 1: return "Argon";
        case 2: return "Pluto / Orion";
        case 3: return "Spitfire";
        case 4: return "Thunderbird";
        case 6: return "Palomino";
        case 7: return "Morgan";
        case 8: return "Thoroughbred";
        case 10: return "Barton / Thorton";
        }
    }
    if (fam == 15) return "K8";
    return "";
}

static const char *intel_name(int fam, int model)
{
    if (fam == 4) return "Intel 486";
    if (fam == 5) return model >= 4 ? "Intel Pentium MMX" : "Intel Pentium";
    if (fam == 6) {
        switch (model) {
        case 1: return "Intel Pentium Pro";
        case 3: case 5: return "Intel Pentium II";
        case 6: return "Intel Celeron";
        case 7: case 8: case 10: case 11: return "Intel Pentium III";
        case 9: case 13: return "Intel Pentium M";
        }
        return "Intel P6 family";
    }
    if (fam == 15) return "Intel Pentium 4";
    return "Intel processor";
}

static const char *intel_core(int fam, int model)
{
    if (fam == 6) {
        switch (model) {
        case 3: return "Klamath";
        case 5: return "Deschutes";
        case 6: return "Mendocino";
        case 7: return "Katmai";
        case 8: return "Coppermine";
        case 9: return "Banias";
        case 10: return "Coppermine T";
        case 11: return "Tualatin";
        case 13: return "Dothan";
        }
    }
    if (fam == 15) {
        switch (model) {
        case 0: case 1: return "Willamette";
        case 2: return "Northwood";
        case 3: case 4: return "Prescott";
        }
    }
    return "";
}

/* Intel cache descriptors (CPUID leaf 2) that name an L1 data or L2 size */
static void intel_cache(int *l1, int *l2)
{
    static const WORD d[][3] = {
        {0x0A, 1, 8}, {0x0C, 1, 16}, {0x2C, 1, 32}, {0x60, 1, 16}, {0x66, 1, 8}, {0x67, 1, 16}, {0x68, 1, 32},
        {0x41, 2, 128}, {0x42, 2, 256}, {0x43, 2, 512}, {0x44, 2, 1024}, {0x45, 2, 2048},
        {0x79, 2, 128}, {0x7A, 2, 256}, {0x7B, 2, 512}, {0x7C, 2, 1024}, {0x82, 2, 256},
        {0x83, 2, 512}, {0x84, 2, 1024}, {0x85, 2, 2048}, {0x86, 2, 512}, {0x87, 2, 1024}
    };
    DWORD r[4];
    int i, j, k;
    cpuid(2, r);
    for (i = 0; i < 4; i++) {
        if (r[i] & 0x80000000u) continue;
        for (j = (i == 0 ? 1 : 0); j < 4; j++) {
            BYTE b = (BYTE)(r[i] >> (j * 8));
            for (k = 0; k < (int)COUNTOF(d); k++) {
                if (d[k][0] != b) continue;
                if (d[k][1] == 1) *l1 += d[k][2];
                else *l2 = d[k][2];
            }
        }
    }
}

static void measure_mhz(CpuInfo *c)
{
    LARGE_INTEGER f, a, b;
    double t0, t1, secs;
    HANDLE th = GetCurrentThread();
    int pri = GetThreadPriority(th);
    if (!c->hasTsc || !QueryPerformanceFrequency(&f) || f.QuadPart == 0) return;
    SetThreadPriority(th, THREAD_PRIORITY_TIME_CRITICAL);
    QueryPerformanceCounter(&a);
    t0 = rdtsc();
    Sleep(120);
    QueryPerformanceCounter(&b);
    t1 = rdtsc();
    SetThreadPriority(th, pri);
    secs = (double)(b.QuadPart - a.QuadPart) / (double)f.QuadPart;
    if (secs > 0) c->mhz = rt_round((t1 - t0) / secs / 1000000.0);
}

void hw_cpu(CpuInfo *c)
{
    DWORD r[4], maxl, maxe = 0;
    int l1 = 0, l2 = 0;
    char tmp[64];
    ZERO(*c);
    if (!has_cpuid()) {
        rt_cpy(c->name, "Processor without CPUID (386 or early 486)", sizeof(c->name));
        rt_cpy(c->vendor, "Unknown", sizeof(c->vendor));
        return;
    }
    cpuid(0, r);
    maxl = r[0];
    memcpy(c->vendor, &r[1], 4);
    memcpy(c->vendor + 4, &r[3], 4);
    memcpy(c->vendor + 8, &r[2], 4);
    c->vendor[12] = 0;
    if (maxl >= 1) {
        cpuid(1, r);
        c->family = (r[0] >> 8) & 15;
        c->model = (r[0] >> 4) & 15;
        c->stepping = r[0] & 15;
        if (c->family == 15) {
            c->family += (r[0] >> 20) & 0xFF;
            c->model |= ((r[0] >> 16) & 15) << 4;
        }
        c->hasTsc = (r[3] >> 4) & 1;
        c->hasMmx = (r[3] >> 23) & 1;
        c->hasSse = (r[3] >> 25) & 1;
    }
    cpuid(0x80000000u, r);
    if (r[0] >= 0x80000000u && r[0] < 0x8000FFFFu) maxe = r[0];
    if (maxe >= 0x80000004u) {
        DWORD b[12];
        cpuid(0x80000002u, b);
        cpuid(0x80000003u, b + 4);
        cpuid(0x80000004u, b + 8);
        memcpy(tmp, b, 48);
        tmp[48] = 0;
        rt_cpy(c->name, rt_trim(tmp), sizeof(c->name));
    }
    if (rt_ieq(c->vendor, "AuthenticAMD")) {
        if (!c->name[0]) wsprintfA(c->name, "AMD %s", amd_core(c->family, c->model));
        rt_cpy(c->core, amd_core(c->family, c->model), sizeof(c->core));
        if (maxe >= 0x80000005u) { cpuid(0x80000005u, r); l1 = (int)(r[2] >> 24); }
        if (maxe >= 0x80000006u) { cpuid(0x80000006u, r); l2 = (int)(r[2] >> 16); }
    } else if (rt_ieq(c->vendor, "GenuineIntel")) {
        if (!c->name[0]) rt_cpy(c->name, intel_name(c->family, c->model), sizeof(c->name));
        rt_cpy(c->core, intel_core(c->family, c->model), sizeof(c->core));
        if (maxl >= 2) intel_cache(&l1, &l2);
        if (!l2 && maxe >= 0x80000006u) { cpuid(0x80000006u, r); l2 = (int)(r[2] >> 16); }
    } else {
        if (!c->name[0]) wsprintfA(c->name, "%s family %d", c->vendor, c->family);
        if (maxe >= 0x80000005u) { cpuid(0x80000005u, r); l1 = (int)(r[2] >> 24); }
        if (maxe >= 0x80000006u) { cpuid(0x80000006u, r); l2 = (int)(r[2] >> 16); }
    }
    if (!c->core[0]) wsprintfA(c->core, "Family %d, model %d", c->family, c->model);
    else {
        wsprintfA(tmp, "%s (family %d, model %d)", c->core, c->family, c->model);
        rt_cpy(c->core, tmp, sizeof(c->core));
    }
    if (l1 || l2) wsprintfA(c->cache, "%d KB / %d KB", l1, l2);
    else rt_cpy(c->cache, "Unknown", sizeof(c->cache));
    measure_mhz(c);
    if (!c->mhz) {
        DWORD mhz = 0, sz = sizeof(mhz);
        HKEY k;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ, &k) == ERROR_SUCCESS) {
            RegQueryValueExA(k, "~MHz", 0, 0, (BYTE *)&mhz, &sz);
            RegCloseKey(k);
        }
        c->mhz = (int)mhz;
    }
}

/* ------------------------------------------------------------------ */
/* graphics                                                            */

typedef struct {
    DWORD cb;
    CHAR DeviceName[32];
    CHAR DeviceString[128];
    DWORD StateFlags;
    CHAR DeviceID[128];
    CHAR DeviceKey[128];
} NFS_DISPLAY_DEVICEA;
typedef BOOL (WINAPI *ENUMDISPLAYDEVICES_)(LPCSTR, DWORD, NFS_DISPLAY_DEVICEA *, DWORD);

void hw_gpu(GpuInfo *g)
{
    HMODULE u = GetModuleHandleA("USER32.DLL");
    ENUMDISPLAYDEVICES_ edd = u ? (ENUMDISPLAYDEVICES_)GetProcAddress(u, "EnumDisplayDevicesA") : 0;
    NFS_DISPLAY_DEVICEA dd;
    char key[160], tmp[128];
    HDC dc;
    DWORD i;
    ZERO(*g);
    key[0] = 0;
    if (edd) {
        for (i = 0; i < 16; i++) {
            ZERO(dd);
            dd.cb = sizeof(dd);
            if (!edd(0, i, &dd, 0)) break;
            if (i == 0 || (dd.StateFlags & 4 /* PRIMARY */)) {
                rt_cpy(g->name, rt_trim(dd.DeviceString), sizeof(g->name));
                rt_cpy(key, dd.DeviceKey, sizeof(key));
                if (dd.StateFlags & 4) break;
            }
        }
    }
    if (key[0]) {
        const char *p = key;
        if (rt_starts(p, "\\Registry\\Machine\\")) p += 18;
        if (reg_get(HKEY_LOCAL_MACHINE, p, "HardwareInformation.MemorySize", tmp, sizeof(tmp), 0) >= 4) {
            DWORD m = *(DWORD *)tmp;
            if (m >= 1024 * 1024) wsprintfA(g->memory, "%lu MB", m / (1024 * 1024));
        }
        reg_str(HKEY_LOCAL_MACHINE, p, "HardwareInformation.ChipType", g->chip, sizeof(g->chip));
        if (!reg_str(HKEY_LOCAL_MACHINE, p, "DriverVersion", g->driver, sizeof(g->driver)))
            if (!reg_str(HKEY_LOCAL_MACHINE, p, "Ver", g->driver, sizeof(g->driver)))
                reg_str(HKEY_LOCAL_MACHINE, p, "InstalledDisplayDrivers", g->driver, sizeof(g->driver));
        if (!g->name[0]) reg_str(HKEY_LOCAL_MACHINE, p, "DriverDesc", g->name, sizeof(g->name));
    }
    if (!g->name[0])
        reg_str(HKEY_LOCAL_MACHINE, "System\\CurrentControlSet\\Services\\Class\\Display\\0000", "DriverDesc", g->name, sizeof(g->name));
    if (!g->name[0]) rt_cpy(g->name, "Unknown display adapter", sizeof(g->name));
    if (!g->memory[0]) rt_cpy(g->memory, "Unknown", sizeof(g->memory));
    if (!g->chip[0]) rt_cpy(g->chip, "Unknown", sizeof(g->chip));
    if (!g->driver[0]) rt_cpy(g->driver, "Unknown", sizeof(g->driver));
    dc = GetDC(0);
    if (dc) {
        int hz = GetDeviceCaps(dc, VREFRESH);
        if (hz > 1)
            wsprintfA(g->mode, "%d x %d, %d bit, %d Hz", GetDeviceCaps(dc, HORZRES), GetDeviceCaps(dc, VERTRES),
                      GetDeviceCaps(dc, BITSPIXEL) * GetDeviceCaps(dc, PLANES), hz);
        else
            wsprintfA(g->mode, "%d x %d, %d bit", GetDeviceCaps(dc, HORZRES), GetDeviceCaps(dc, VERTRES),
                      GetDeviceCaps(dc, BITSPIXEL) * GetDeviceCaps(dc, PLANES));
        ReleaseDC(0, dc);
    }
}

/* ------------------------------------------------------------------ */
/* motherboard: chipset (PCI host bridge) and BIOS from the registry   */

static const struct { WORD ven, dev; const char *name; } g_bridges[] = {
    {0x8086, 0x7190, "Intel 440BX"}, {0x8086, 0x7192, "Intel 440BX (no AGP)"}, {0x8086, 0x7180, "Intel 440LX"},
    {0x8086, 0x7124, "Intel 810E"}, {0x8086, 0x1130, "Intel 815"}, {0x8086, 0x2500, "Intel 820"},
    {0x8086, 0x1A30, "Intel 845"}, {0x8086, 0x2560, "Intel 845G"}, {0x8086, 0x2570, "Intel 865 / 848"},
    {0x8086, 0x2578, "Intel 875P"}, {0x8086, 0x3340, "Intel 855PM"}, {0x8086, 0x3580, "Intel 852 / 855GM"},
    {0x1106, 0x0691, "VIA Apollo Pro 133"}, {0x1106, 0x0305, "VIA KT133 / KT133A"}, {0x1106, 0x3099, "VIA KT266 / KT333"},
    {0x1106, 0x3189, "VIA KT400 / KT600"}, {0x1106, 0x0598, "VIA Apollo MVP3"}, {0x1106, 0x3205, "VIA KM400"},
    {0x1106, 0x3168, "VIA PT800"}, {0x1106, 0x0282, "VIA K8T800"}, {0x10DE, 0x01E0, "NVIDIA nForce2"},
    {0x10DE, 0x01A4, "NVIDIA nForce"}, {0x1039, 0x0735, "SiS 735"}, {0x1039, 0x0746, "SiS 746"},
    {0x1039, 0x0648, "SiS 648"}, {0x1039, 0x0530, "SiS 530"}, {0x1022, 0x7006, "AMD 751"},
    {0x1022, 0x700C, "AMD 762"}, {0x1002, 0x5950, "ATI RS480"}, {0x10B9, 0x1541, "ALi Aladdin V"}
};

static int hexval(const char *s, int n)
{
    int v = 0, i;
    for (i = 0; i < n; i++) {
        char c = s[i];
        v <<= 4;
        if (c >= '0' && c <= '9') v |= c - '0';
        else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
        else return -1;
    }
    return v;
}

static int find_host_bridge(const char *enumPath, int isNT, char *out, int cap)
{
    HKEY k, k2;
    char dev[128], inst[128], path[300], val[160];
    DWORD i, j, n;
    int found = 0;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, enumPath, 0, KEY_READ, &k) != ERROR_SUCCESS) return 0;
    for (i = 0; !found; i++) {
        n = sizeof(dev);
        if (RegEnumKeyExA(k, i, dev, &n, 0, 0, 0, 0) != ERROR_SUCCESS) break;
        wsprintfA(path, "%s\\%s", enumPath, dev);
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &k2) != ERROR_SUCCESS) continue;
        for (j = 0; !found; j++) {
            int hit;
            n = sizeof(inst);
            if (RegEnumKeyExA(k2, j, inst, &n, 0, 0, 0, 0) != ERROR_SUCCESS) break;
            wsprintfA(path, "%s\\%s\\%s", enumPath, dev, inst);
            if (isNT) {
                reg_str(HKEY_LOCAL_MACHINE, path, "LocationInformation", val, sizeof(val));
                hit = rt_find(val, "bus 0, device 0, function 0") != 0;
            } else {
                hit = rt_find(inst, "BUS_00&DEV_00&FUNC_00") != 0;
            }
            if (!hit) continue;
            {
                const char *v = rt_find(dev, "VEN_"), *d = rt_find(dev, "DEV_");
                int ven = v ? hexval(v + 4, 4) : -1, did = d ? hexval(d + 4, 4) : -1, m;
                for (m = 0; m < (int)COUNTOF(g_bridges); m++)
                    if (g_bridges[m].ven == ven && g_bridges[m].dev == did) { rt_cpy(out, g_bridges[m].name, cap); found = 1; }
                if (!found && reg_str(HKEY_LOCAL_MACHINE, path, "DeviceDesc", val, sizeof(val))) { rt_cpy(out, val, cap); found = 1; }
                if (!found && ven >= 0) { wsprintfA(out, "PCI %04Xh:%04Xh", ven, did); found = 1; }
            }
        }
        RegCloseKey(k2);
    }
    RegCloseKey(k);
    return found;
}

void hw_board(HwInfo *hw)
{
    char a[96], b[64];
    MEMORYSTATUS ms;
    hw->chipset[0] = 0;
    if (hw->os.isNT) find_host_bridge("SYSTEM\\CurrentControlSet\\Enum\\PCI", 1, hw->chipset, sizeof(hw->chipset));
    else find_host_bridge("Enum\\PCI", 0, hw->chipset, sizeof(hw->chipset));
    if (!hw->chipset[0]) rt_cpy(hw->chipset, "Unknown", sizeof(hw->chipset));

    hw->bios[0] = 0;
    if (hw->os.isNT) {
        reg_str(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System", "SystemBiosVersion", a, sizeof(a));
        reg_str(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System", "SystemBiosDate", b, sizeof(b));
    } else {
        char n[48];
        reg_str(HKEY_LOCAL_MACHINE, "Enum\\Root\\*PNP0C01\\0000", "BIOSName", n, sizeof(n));
        reg_str(HKEY_LOCAL_MACHINE, "Enum\\Root\\*PNP0C01\\0000", "BIOSVersion", a, sizeof(a));
        reg_str(HKEY_LOCAL_MACHINE, "Enum\\Root\\*PNP0C01\\0000", "BIOSDate", b, sizeof(b));
        if (n[0] && !rt_find(a, n)) {
            char t[96];
            wsprintfA(t, "%s %s", n, a);
            rt_cpy(a, rt_trim(t), sizeof(a));
        }
    }
    if (a[0] && b[0]) wsprintfA(hw->bios, "%s, %s", a, b);
    else if (a[0]) rt_cpy(hw->bios, a, sizeof(hw->bios));
    else if (b[0]) rt_cpy(hw->bios, b, sizeof(hw->bios));
    else rt_cpy(hw->bios, "Unknown", sizeof(hw->bios));

    ZERO(ms);
    ms.dwLength = sizeof(ms);
    GlobalMemoryStatus(&ms);
    hw->memMB = (int)((ms.dwTotalPhys + 1024 * 1024 - 1) / (1024 * 1024));
    /* the BIOS and video memory take a little; round up to 4 MB */
    hw->memMB = (hw->memMB + 3) & ~3;
}

/* ------------------------------------------------------------------ */
/* sensor chip                                                         */

static BYTE mon_rd(WORD base, BYTE reg)
{
    outb((WORD)(base + 5), reg);
    return inb((WORD)(base + 6));
}

static void mon_wr(WORD base, BYTE reg, BYTE v)
{
    outb((WORD)(base + 5), reg);
    outb((WORD)(base + 6), v);
}

static void wb_bank(WORD base, int bank)
{
    mon_wr(base, 0x4E, (BYTE)(0x80 | (bank & 7)));
}

static int wb_vendor_ok(WORD base)
{
    BYTE hi, lo;
    if (inb((WORD)(base + 5)) == 0xFF && inb((WORD)(base + 6)) == 0xFF) return 0;
    mon_wr(base, 0x4E, 0x80);
    hi = mon_rd(base, 0x4F);
    mon_wr(base, 0x4E, 0x00);
    lo = mon_rd(base, 0x4F);
    mon_wr(base, 0x4E, 0x80);
    return hi == 0x5C && lo == 0xA3;
}

static void sio_winbond(WORD idx, ChipInfo *c)
{
    BYTE id;
    outb(idx, 0x87);
    outb(idx, 0x87);
    outb(idx, 0x20);
    id = inb((WORD)(idx + 1));
    if (id == 0x52 || id == 0x60 || id == 0x82 || id == 0x70 || id == 0x88 || id == 0x85 || id == 0xA0) {
        BYTE hi, lo;
        c->devId = id;
        outb(idx, 0x07); outb((WORD)(idx + 1), 0x0B);       /* hardware monitor device */
        outb(idx, 0x60); hi = inb((WORD)(idx + 1));
        outb(idx, 0x61); lo = inb((WORD)(idx + 1));
        c->base = (WORD)(((hi << 8) | lo) & 0xFFF8);
        c->kind = CHIP_WINBOND;
        switch (id) {
        case 0x52: rt_cpy(c->name, "Winbond W83627HF", sizeof(c->name)); break;
        case 0x60: rt_cpy(c->name, "Winbond W83697HF", sizeof(c->name)); break;
        case 0x82: rt_cpy(c->name, "Winbond W83627THF", sizeof(c->name)); break;
        case 0x70: rt_cpy(c->name, "Winbond W83637HF", sizeof(c->name)); break;
        case 0x88: rt_cpy(c->name, "Winbond W83627EHF", sizeof(c->name)); break;
        case 0x85: rt_cpy(c->name, "Winbond W83687THF", sizeof(c->name)); break;
        default:   rt_cpy(c->name, "Winbond W83627DHG", sizeof(c->name)); break;
        }
    }
    outb(idx, 0xAA);
}

static void sio_ite(WORD idx, ChipInfo *c)
{
    BYTE a, b;
    outb(idx, 0x87);
    outb(idx, 0x01);
    outb(idx, 0x55);
    outb(idx, idx == 0x2E ? 0x55 : 0xAA);
    outb(idx, 0x20); a = inb((WORD)(idx + 1));
    outb(idx, 0x21); b = inb((WORD)(idx + 1));
    if (a == 0x87 && b != 0xFF && b != 0x00) {
        BYTE hi, lo;
        c->devId = (a << 8) | b;
        outb(idx, 0x07); outb((WORD)(idx + 1), 0x04);       /* environment controller */
        outb(idx, 0x60); hi = inb((WORD)(idx + 1));
        outb(idx, 0x61); lo = inb((WORD)(idx + 1));
        c->base = (WORD)(((hi << 8) | lo) & 0xFFF8);
        c->kind = CHIP_ITE;
        wsprintfA(c->name, "ITE IT87%02XF", b);
    }
    outb(idx, 0x02);
    outb((WORD)(idx + 1), 0x02);
}

static int wb_fan_div(WORD base, int fan)
{
    BYTE r47, r4b, r5d;
    int lo;
    wb_bank(base, 0);
    r47 = mon_rd(base, 0x47);
    r4b = mon_rd(base, 0x4B);
    r5d = mon_rd(base, 0x5D);
    if (fan == 0) lo = (r47 >> 4) & 3;
    else if (fan == 1) lo = (r47 >> 6) & 3;
    else lo = (r4b >> 6) & 3;
    lo |= ((r5d >> (5 + fan)) & 1) << 2;
    return 1 << lo;
}

void hw_chip_detect(HwInfo *hw)
{
    ChipInfo *c = &hw->chip;
    int i;
    ZERO(*c);
    for (i = 0; i < NFAN_CHIP; i++) c->pwmSaved[i] = -1;
    if (hw->demo) {
        c->kind = CHIP_DEMO;
        c->supported = c->canControl = 1;
        rt_cpy(c->name, "Demo sensor chip", sizeof(c->name));
        rt_cpy(c->where, "Simulated", sizeof(c->where));
        for (i = 0; i < NTEMP_CHIP; i++) c->hasTemp[i] = 1;
        c->hasFan[0] = c->hasFan[1] = c->hasFan[2] = 1;
        c->fanCtl[0] = c->fanCtl[1] = 1;
        return;
    }
    if (!hw->ioAllowed) return;

    sio_winbond(0x2E, c);
    if (!c->kind) sio_winbond(0x4E, c);
    if (!c->kind) sio_ite(0x2E, c);
    if (!c->kind) sio_ite(0x4E, c);
    if (!c->kind && wb_vendor_ok(0x290)) {
        /* older boards: the monitor sits at 290h with no Super I/O entry */
        c->kind = CHIP_WINBOND;
        c->base = 0x290;
        wb_bank(0x290, 0);
        switch (mon_rd(0x290, 0x58)) {
        case 0x10: rt_cpy(c->name, "Winbond W83781D", sizeof(c->name)); break;
        case 0x30: rt_cpy(c->name, "Winbond W83782D", sizeof(c->name)); break;
        case 0x40: rt_cpy(c->name, "Winbond W83783S", sizeof(c->name)); break;
        case 0x21: rt_cpy(c->name, "Winbond W83627HF", sizeof(c->name)); c->devId = 0x52; break;
        case 0x60: rt_cpy(c->name, "Winbond W83697HF", sizeof(c->name)); c->devId = 0x60; break;
        default:   rt_cpy(c->name, "Winbond (unknown model)", sizeof(c->name)); break;
        }
    }
    if (!c->kind) return;
    if (c->base < 0x100 || c->base == 0xFFF8) {
        c->kind = CHIP_UNKNOWN;
        rt_cat(c->name, " (monitor turned off)", sizeof(c->name));
        return;
    }
    wsprintfA(c->where, "ISA port %Xh", c->base);

    if (c->kind == CHIP_WINBOND) {
        if (c->devId == 0x88 || c->devId == 0x85 || c->devId == 0xA0 || !wb_vendor_ok(c->base)) {
            /* newer register layout, not handled */
            c->kind = CHIP_UNKNOWN;
            return;
        }
        c->supported = 1;
        c->hasTemp[0] = c->hasTemp[1] = 1;
        c->hasTemp[2] = c->devId != 0x60;              /* W83697HF has two */
        if (c->devId == 0x52 || c->devId == 0x60) {
            c->canControl = 1;
            c->fanCtl[0] = c->fanCtl[1] = 1;
            c->pwmReg[0] = c->devId == 0x60 ? 0x01 : 0x5A;
            c->pwmReg[1] = c->devId == 0x60 ? 0x03 : 0x5B;
        }
    } else if (c->kind == CHIP_ITE) {
        /* IT8716F and IT8718F may count fans in 16 bit mode; not handled */
        if ((c->devId & 0xFF) != 0x05 && (c->devId & 0xFF) != 0x12) { c->kind = CHIP_UNKNOWN; return; }
        if (mon_rd(c->base, 0x58) != 0x90) { c->kind = CHIP_UNKNOWN; return; }
        c->supported = 1;
        c->hasTemp[0] = c->hasTemp[1] = c->hasTemp[2] = 1;
    }
    {
        ChipReading r;
        hw_chip_read(hw, &r);
        for (i = 0; i < NFAN_CHIP; i++) {
            c->hasFan[i] = r.rpmOk[i];
            if (!c->hasFan[i]) c->fanCtl[i] = 0;
        }
        for (i = 0; i < NTEMP_CHIP; i++)
            if (c->hasTemp[i] && !r.tempOk[i]) c->hasTemp[i] = 0;
    }
}

static double g_demoT[3] = { 36, 52, 42 };

int hw_chip_read(HwInfo *hw, ChipReading *r)
{
    ChipInfo *c = &hw->chip;
    int i;
    ZERO(*r);
    if (c->kind == CHIP_DEMO) {
        static const int maxr[3] = { 5400, 2600, 1850 };
        double duty0 = c->pwmNow[0] ? c->pwmNow[0] : 65, duty1 = c->pwmNow[1] ? c->pwmNow[1] : 45;
        double tgt[3];
        tgt[0] = 29 + (100 - duty1) * 0.12;          /* board */
        tgt[1] = 44 + (100 - duty0) * 0.3;           /* CPU diode */
        tgt[2] = 37 + (100 - duty0) * 0.14;
        for (i = 0; i < 3; i++) {
            g_demoT[i] += (tgt[i] - g_demoT[i]) * 0.12 + (rt_rand() - 0.5) * 0.5;
            r->temp[i] = g_demoT[i];
            r->tempOk[i] = 1;
        }
        r->rpm[0] = rt_round(maxr[0] * duty0 / 100 * (1 + (rt_rand() - 0.5) * 0.01));
        r->rpm[1] = rt_round(maxr[1] * duty1 / 100 * (1 + (rt_rand() - 0.5) * 0.01));
        r->rpm[2] = rt_round(maxr[2] * (1 + (rt_rand() - 0.5) * 0.01));
        r->rpmOk[0] = r->rpmOk[1] = r->rpmOk[2] = 1;
        r->vcore = 1.65 + (rt_rand() - 0.5) * 0.02;
        r->v33 = 3.31 + (rt_rand() - 0.5) * 0.02;
        r->v5 = 4.97 + (rt_rand() - 0.5) * 0.03;
        r->v12 = 12.04 + (rt_rand() - 0.5) * 0.08;
        r->voltOk = 1;
        return 1;
    }
    if (!c->supported || !hw->ioAllowed) return 0;

    if (c->kind == CHIP_WINBOND) {
        WORD b = c->base;
        BYTE t;
        wb_bank(b, 0);
        t = mon_rd(b, 0x27);
        r->temp[0] = (signed char)t;
        r->tempOk[0] = t != 0xFF && t != 0x80 && (signed char)t > -30;
        wb_bank(b, 1);
        t = mon_rd(b, 0x50);
        r->temp[1] = (signed char)t + ((mon_rd(b, 0x51) & 0x80) ? 0.5 : 0);
        r->tempOk[1] = t != 0xFF && t != 0x80 && (signed char)t > -30;
        if (c->hasTemp[2] || !c->supported) {
            wb_bank(b, 2);
            t = mon_rd(b, 0x50);
            r->temp[2] = (signed char)t + ((mon_rd(b, 0x51) & 0x80) ? 0.5 : 0);
            r->tempOk[2] = t != 0xFF && t != 0x80 && (signed char)t > -30;
        }
        for (i = 0; i < NFAN_CHIP; i++) {
            int div = wb_fan_div(b, i);
            BYTE cnt;
            wb_bank(b, 0);
            cnt = mon_rd(b, (BYTE)(0x28 + i));
            if (cnt != 0xFF && cnt != 0) {
                r->rpmOk[i] = 1;
                r->rpm[i] = (int)(1350000L / ((long)cnt * div));
            } else {
                /* a fan found at start that reads FFh is slower than the
                   counter can measure with this divisor, or stopped */
                r->rpmOk[i] = c->hasFan[i];
                r->rpm[i] = 0;
                if (cnt == 0xFF) r->rpmUnder[i] = (int)(1350000L / (255L * div));
            }
        }
        wb_bank(b, 0);
        r->vcore = mon_rd(b, 0x20) * 0.016;
        r->v33 = mon_rd(b, 0x22) * 0.016;
        r->v5 = mon_rd(b, 0x23) * 0.016 * 1.68;
        r->v12 = mon_rd(b, 0x24) * 0.016 * 3.8;
        r->voltOk = 1;
        return 1;
    }
    if (c->kind == CHIP_ITE) {
        WORD b = c->base;
        BYTE dv = mon_rd(b, 0x0B);
        int divs[3];
        divs[0] = 1 << (dv & 7);
        divs[1] = 1 << ((dv >> 3) & 7);
        divs[2] = (dv & 0x40) ? 8 : 2;
        for (i = 0; i < 3; i++) {
            BYTE t = mon_rd(b, (BYTE)(0x29 + i));
            r->temp[i] = (signed char)t;
            r->tempOk[i] = t != 0xFF && t != 0x80 && (signed char)t > -30;
        }
        for (i = 0; i < 3; i++) {
            BYTE cnt = mon_rd(b, (BYTE)(0x0D + i));
            if (cnt != 0xFF && cnt != 0) {
                r->rpmOk[i] = 1;
                r->rpm[i] = (int)(1350000L / ((long)cnt * divs[i]));
            } else {
                r->rpmOk[i] = c->hasFan[i];
                r->rpm[i] = 0;
                if (cnt == 0xFF) r->rpmUnder[i] = (int)(1350000L / (255L * divs[i]));
            }
        }
        r->vcore = mon_rd(b, 0x20) * 0.016;
        r->v33 = mon_rd(b, 0x22) * 0.016;
        r->v5 = mon_rd(b, 0x23) * 0.016 * 1.68;
        r->v12 = mon_rd(b, 0x24) * 0.016 * 4.0;
        r->voltOk = 1;
        return 1;
    }
    return 0;
}

void hw_chip_pwm(HwInfo *hw, int fan, int duty)
{
    ChipInfo *c = &hw->chip;
    if (fan < 0 || fan >= NFAN_CHIP || !c->fanCtl[fan]) return;
    if (c->kind == CHIP_DEMO) {
        c->pwmNow[fan] = duty < 0 ? (fan == 0 ? 70 : 50) : duty;
        return;
    }
    if (c->kind != CHIP_WINBOND || !hw->ioAllowed) return;
    wb_bank(c->base, 0);
    if (duty < 0) {
        /* hand the fan back: put back what the BIOS had set */
        if (c->pwmSaved[fan] >= 0) mon_wr(c->base, (BYTE)c->pwmReg[fan], (BYTE)c->pwmSaved[fan]);
        c->pwmNow[fan] = 0;
        return;
    }
    if (duty > 100) duty = 100;
    if (c->pwmSaved[fan] < 0) c->pwmSaved[fan] = mon_rd(c->base, (BYTE)c->pwmReg[fan]);
    mon_wr(c->base, (BYTE)c->pwmReg[fan], (BYTE)(duty * 255 / 100));
    c->pwmNow[fan] = duty;
}

void hw_chip_restore(HwInfo *hw)
{
    int i;
    for (i = 0; i < NFAN_CHIP; i++)
        if (hw->chip.fanCtl[i]) hw_chip_pwm(hw, i, -1);
}

/* ------------------------------------------------------------------ */
/* S.M.A.R.T. drive temperature                                        */

#pragma pack(push, 1)
typedef struct {
    BYTE features, count, number, cylLow, cylHigh, driveHead, command, reserved;
} NFS_IDEREGS;
typedef struct {
    DWORD bufferSize;
    NFS_IDEREGS regs;
    BYTE driveNumber;
    BYTE reserved[3];
    DWORD reserved2[4];
    BYTE buffer[1];
} NFS_SENDCMDIN;
typedef struct {
    BYTE driverError, ideError, reserved[2];
    DWORD reserved2[2];
} NFS_DRIVERSTATUS;
typedef struct {
    DWORD bufferSize;
    NFS_DRIVERSTATUS status;
    BYTE buffer[512];
} NFS_SENDCMDOUT;
#pragma pack(pop)

#define NFS_SMART_RCV 0x0007C088

static HANDLE smart_open(HwInfo *hw, int drive)
{
    char name[32];
    if (!hw->os.isNT)
        return CreateFileA("\\\\.\\SMARTVSD", 0, 0, 0, CREATE_NEW, 0, 0);
    wsprintfA(name, "\\\\.\\PhysicalDrive%d", drive);
    return CreateFileA(name, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, 0, 0);
}

static int smart_query(HwInfo *hw, int drive, double *t)
{
    HANDLE h = smart_open(hw, drive);
    NFS_SENDCMDIN in;
    NFS_SENDCMDOUT out;
    DWORD got = 0;
    int i, ok = 0;
    if (h == INVALID_HANDLE_VALUE) return 0;
    ZERO(in);
    ZERO(out);
    in.bufferSize = 512;
    in.regs.features = 0xD0;          /* read attribute values */
    in.regs.count = 1;
    in.regs.number = 1;
    in.regs.cylLow = 0x4F;
    in.regs.cylHigh = 0xC2;
    in.regs.driveHead = (BYTE)(0xA0 | ((drive & 1) << 4));
    in.regs.command = 0xB0;
    in.driveNumber = (BYTE)drive;
    if (DeviceIoControl(h, NFS_SMART_RCV, &in, sizeof(in) - 1, &out, sizeof(out), &got, 0)) {
        BYTE *a = out.buffer + 2;
        for (i = 0; i < 30; i++, a += 12) {
            if (a[0] == 194 || a[0] == 190) {
                int v = a[5];
                if (v > 0 && v < 100) { *t = v; ok = 1; }
                if (a[0] == 194 && ok) break;
            }
        }
    }
    CloseHandle(h);
    return ok;
}

int hw_smart_scan(HwInfo *hw)
{
    int d;
    double t;
    hw->smartDrives = 0;
    hw->smartDrive = -1;
    if (hw->demo) { hw->smartDrives = 1; hw->smartDrive = 0; return 1; }
    if (hw->os.isNT && !hw->os.isAdmin) return 0;
    for (d = 0; d < 4; d++) {
        if (smart_query(hw, d, &t)) {
            if (hw->smartDrive < 0) hw->smartDrive = d;
            hw->smartDrives++;
        }
    }
    return hw->smartDrives;
}

int hw_smart_temp(HwInfo *hw, double *t)
{
    static double demo = 37.5;
    if (hw->demo) {
        demo += (37.6 - demo) * 0.1 + (rt_rand() - 0.5) * 0.2;
        *t = demo;
        return 1;
    }
    if (hw->smartDrive < 0) return 0;
    return smart_query(hw, hw->smartDrive, t);
}
