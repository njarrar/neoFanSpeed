/* Shared state for neoFanSpeed */
#ifndef NFS_APP_H
#define NFS_APP_H

#include <windows.h>
#include <commctrl.h>
#include "hw.h"

#define APP_NAME   "neoFanSpeed"
#define APP_VER    "3.0"

#define NSENS 5
#define NFAN  3
#define HIST  60
#define NPTS  4

enum { S_CPU = 0, S_BOARD, S_AUX, S_GPU, S_HDD, S_HOT = 9 };
enum { M_AUTO = 0, M_MANUAL, M_PROFILE, M_CURVE };
enum { P_SILENT = 0, P_NORMAL, P_FULL };
enum { HWS_OK = 0, HWS_NT, HWS_NOCHIP, HWS_UNSUPPORTED };

typedef struct {
    char name[40];
    char shortName[12];
    COLORREF color;
    int dash;               /* pen style for the graph */
    int avail;
    int plot;
    double val;
    double min, max, sum;
    long n;
    double limit;
    double hist[HIST];
    int nhist;
    int alerted;
} Sensor;

typedef struct {
    char name[40];
    char shortName[12];
    char header[24];
    int avail;              /* speed can be read */
    int ctrl;               /* speed can be set */
    int mode;
    int pct;                /* manual duty */
    int profile;
    int follows;            /* sensor index or S_HOT */
    int pts[NPTS][2];       /* temp C, duty % */
    int alarmOn;
    int alarm;              /* warn below this RPM */
    int allowBelow;         /* manual duty allowed under floor, -1 none */
    double hT;              /* held temperature for hysteresis */
    int rpm;
    int target;             /* expected RPM for the duty */
    int maxRpm;             /* highest RPM seen, for the target guess */
    double duty;
    int lastWritten;
} Fan;

typedef struct {
    int on;
    char path[MAX_PATH];
    int csv;
    int interval;
    int incS[NSENS];
    int incF[NFAN];
    int incD[NFAN];
    int header;
    int roll;
    int onLaunch;
    long rows;
    DWORD bytes;
    int tick;
    char unitAtStart;
    char recent[9][200];
    int nrecent;
    char error[160];
} LogState;

typedef struct {
    int bios;               /* hand fans back on exit */
    int failsafe;
    int floor;
    int warnWin;
    int beep;
    int balloon;
    int hyst;
    int applyAll;
} Options;

typedef struct {
    HINSTANCE inst;
    HWND main, tab, status, banner, bannerBtn, logBtn, resetBtn, profLbl, profile;
    HWND page[4];
    HWND mini;
    HWND alarmDlg;
    HFONT font, bold, ledFont, smallFont;
    HACCEL accel;
    HIMAGELIST swatches;
    int curTab;
    int inUpdate;

    HwInfo hw;
    int hwState;
    ChipReading rd;

    Sensor s[NSENS];
    Fan f[NFAN];
    int selSensor, selFan;
    char unit;              /* 'C' or 'F' */
    Options o;
    LogState log;
    int onTop;
    int miniView;
    int ticks;

    /* fail-safe */
    int fsActive;
    int prevMode[NFAN], prevProfile[NFAN];
    char fsName[40], fsLim[24], fsTime[8];
    int alarmSensor;
    double alarmTemp;

    int trayAdded;
    UINT taskbarMsg;
    int exiting;
    char iniPath[MAX_PATH];
    char exeDir[MAX_PATH];
    int firstRun;
    char reportMsg[120];
} App;

extern App g;

/* app.c */
void app_init_model(void);
void app_tick(int fromTimer);
double app_temp_of(int id);
double app_duty_of(Fan *f);
void app_set_all(int profileOrAuto);  /* P_* or -1 for BIOS */
int  app_cur_profile(void);           /* P_*, -1 BIOS, -2 custom, -3 none */
void app_restore_prev(void);
void app_force_full(int sensor);
void app_reset_stats(void);
double app_cv(double c);
char *app_ft(char *buf, double c);   /* "54.2 °C" */
void app_load_settings(void);
void app_save_settings(void);
int  app_count_ctrl(void);
void app_apply_hw(void);

/* log.c */
void log_toggle(void);
void log_tick(void);
void log_line(char *out, int cap, int forDisplay);
void log_header(char *out, int cap);

/* ui.c */
void ui_refresh(void);
void ui_refresh_static(void);
void ui_status(void);
void ui_show_alarm(int sensor);
void ui_banner(void);
void ui_tray_update(void);
void ui_balloon(const char *title, const char *text);
void ui_layout(void);
void ui_set_tab(int t);
void ui_detect(HWND owner);
void ui_build_report(char *out, int cap);

/* mini.c */
void mini_create(void);
void mini_refresh(void);
LRESULT CALLBACK mini_proc(HWND, UINT, WPARAM, LPARAM);

/* draw.c */
void draw_register(HINSTANCE inst);

#endif
