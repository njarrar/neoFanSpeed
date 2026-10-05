/* Hardware access: Windows version, CPU, graphics, sensor chip, S.M.A.R.T. */
#ifndef NFS_HW_H
#define NFS_HW_H

#include <windows.h>

#define NTEMP_CHIP 3
#define NFAN_CHIP  3

typedef struct {
    int isNT;               /* 2000 / XP family */
    int major, minor, build;
    char name[64];          /* "98 Second Edition (4.10.2222 A)" */
    char shortName[16];     /* "98SE", "ME", "2000", "XP" */
    int hasBalloon;         /* shell32 5.0 or later */
    int isAdmin;            /* NT only, 1 on 9x */
} OsInfo;

typedef struct {
    char name[64];
    char core[64];
    char vendor[16];
    int family, model, stepping;
    int mhz;
    char cache[48];
    int hasTsc, hasMmx, hasSse;
} CpuInfo;

typedef struct {
    char name[96];
    char memory[32];
    char chip[64];
    char driver[64];
    char mode[48];
} GpuInfo;

/* sensor chip state */
enum { CHIP_NONE = 0, CHIP_WINBOND, CHIP_ITE, CHIP_DEMO, CHIP_UNKNOWN };

typedef struct {
    int kind;               /* CHIP_* */
    int supported;          /* readings work */
    int canControl;         /* PWM writes work */
    char name[64];          /* "Winbond W83627HF" */
    char where[32];         /* "ISA port 290h" */
    WORD base;              /* monitor base port */
    int devId;              /* Super I/O device id */
    int hasTemp[NTEMP_CHIP];
    int hasFan[NFAN_CHIP];
    int fanCtl[NFAN_CHIP];  /* fan header has PWM output */
    int pwmReg[NFAN_CHIP];
    int pwmSaved[NFAN_CHIP];/* value found at start, -1 if not saved */
    int pwmNow[NFAN_CHIP];
} ChipInfo;

typedef struct {
    double temp[NTEMP_CHIP];
    int tempOk[NTEMP_CHIP];
    int rpm[NFAN_CHIP];
    int rpmOk[NFAN_CHIP];
    int rpmUnder[NFAN_CHIP];/* counter full: speed is below this RPM */
    double vcore, v33, v5, v12;
    int voltOk;
} ChipReading;

typedef struct {
    OsInfo os;
    CpuInfo cpu;
    GpuInfo gpu;
    ChipInfo chip;
    char chipset[96];
    char bios[96];
    int memMB;
    int memFreeMB;
    int ioAllowed;          /* direct port access is possible */
    int ioReason;           /* why not: 0 ok, 1 NT blocks it, 2 turned off */
    int smartDrives;        /* drives that report a temperature */
    int smartDrive;         /* first drive with a temperature */
    int demo;
} HwInfo;

void hw_os(OsInfo *os);
void hw_cpu(CpuInfo *cpu);
void hw_gpu(GpuInfo *gpu);
int  hw_gpu_temp(HwInfo *hw, double *t);
void hw_board(HwInfo *hw);
void hw_chip_detect(HwInfo *hw);
int  hw_chip_read(HwInfo *hw, ChipReading *r);
void hw_chip_pwm(HwInfo *hw, int fan, int duty);   /* duty 0..100, -1 = hand back */
void hw_chip_restore(HwInfo *hw);
int  hw_smart_scan(HwInfo *hw);
int  hw_smart_temp(HwInfo *hw, double *t);

#endif
