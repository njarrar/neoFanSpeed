/* Small runtime helpers. The app links no C runtime so that the only
   imports are system DLLs that ship with Windows 98 and later. */
#ifndef NFS_RT_H
#define NFS_RT_H

#include <windows.h>

void *memset(void *d, int c, unsigned int n);
void *memcpy(void *d, const void *s, unsigned int n);
void *memmove(void *d, const void *s, unsigned int n);
int memcmp(const void *a, const void *b, unsigned int n);

#define ZERO(x) memset(&(x), 0, sizeof(x))
#define COUNTOF(a) (sizeof(a) / sizeof((a)[0]))

/* string helpers (ANSI) */
int rt_len(const char *s);
char *rt_cpy(char *d, const char *s, int cap);   /* always ends with 0 */
char *rt_cat(char *d, const char *s, int cap);
int rt_ieq(const char *a, const char *b);         /* case blind equal */
int rt_starts(const char *s, const char *p);      /* case blind prefix */
const char *rt_find(const char *s, const char *p);
char *rt_trim(char *s);
int rt_atoi(const char *s);
double rt_atof(const char *s);

/* number formatting */
char *rt_fmt1(char *buf, double v);               /* "54.2" */
char *rt_fmt2(char *buf, double v);               /* "3.31" */
char *rt_fmtint(char *buf, long v);               /* "3,512" */
int rt_round(double v);
double rt_fabs(double v);

/* tiny pseudo random for demo mode */
double rt_rand(void);                             /* 0..1 */
void rt_srand(DWORD seed);

#endif
