/* Small runtime helpers, see rt.h */
#include "rt.h"

void *memset(void *d, int c, unsigned int n)
{
    unsigned char *p = (unsigned char *)d;
    while (n--) *p++ = (unsigned char)c;
    return d;
}

void *memcpy(void *d, const void *s, unsigned int n)
{
    unsigned char *p = (unsigned char *)d;
    const unsigned char *q = (const unsigned char *)s;
    while (n--) *p++ = *q++;
    return d;
}

void *memmove(void *d, const void *s, unsigned int n)
{
    unsigned char *p = (unsigned char *)d;
    const unsigned char *q = (const unsigned char *)s;
    if (p < q) {
        while (n--) *p++ = *q++;
    } else {
        p += n; q += n;
        while (n--) *--p = *--q;
    }
    return d;
}

int memcmp(const void *a, const void *b, unsigned int n)
{
    const unsigned char *p = (const unsigned char *)a, *q = (const unsigned char *)b;
    for (; n; n--, p++, q++)
        if (*p != *q) return *p - *q;
    return 0;
}

int rt_len(const char *s)
{
    int n = 0;
    while (s[n]) n++;
    return n;
}

char *rt_cpy(char *d, const char *s, int cap)
{
    int i = 0;
    if (cap <= 0) return d;
    while (s[i] && i < cap - 1) { d[i] = s[i]; i++; }
    d[i] = 0;
    return d;
}

char *rt_cat(char *d, const char *s, int cap)
{
    int n = rt_len(d);
    if (n < cap) rt_cpy(d + n, s, cap - n);
    return d;
}

static char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

int rt_ieq(const char *a, const char *b)
{
    while (*a && lower(*a) == lower(*b)) { a++; b++; }
    return lower(*a) == lower(*b);
}

int rt_starts(const char *s, const char *p)
{
    while (*p) {
        if (lower(*s) != lower(*p)) return 0;
        s++; p++;
    }
    return 1;
}

const char *rt_find(const char *s, const char *p)
{
    for (; *s; s++)
        if (rt_starts(s, p)) return s;
    return 0;
}

char *rt_trim(char *s)
{
    int n;
    while (*s == ' ' || *s == '\t') s++;
    n = rt_len(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) s[--n] = 0;
    return s;
}

int rt_atoi(const char *s)
{
    int v = 0, neg = 0;
    while (*s == ' ') s++;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    return neg ? -v : v;
}

double rt_atof(const char *s)
{
    double v = 0, f = 0.1;
    int neg = 0;
    while (*s == ' ') s++;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    if (*s == '.') {
        s++;
        while (*s >= '0' && *s <= '9') { v += (*s++ - '0') * f; f /= 10; }
    }
    return neg ? -v : v;
}

int rt_round(double v)
{
    return v < 0 ? -(int)(-v + 0.5) : (int)(v + 0.5);
}

double rt_fabs(double v) { return v < 0 ? -v : v; }

static char *fmtfix(char *buf, double v, int places)
{
    long scale = places == 1 ? 10 : 100;
    long n;
    int neg = v < 0;
    if (neg) v = -v;
    n = (long)(v * scale + 0.5);
    if (places == 1)
        wsprintfA(buf, "%s%ld.%ld", (neg && n) ? "-" : "", n / 10, n % 10);
    else
        wsprintfA(buf, "%s%ld.%02ld", (neg && n) ? "-" : "", n / 100, n % 100);
    return buf;
}

char *rt_fmt1(char *buf, double v) { return fmtfix(buf, v, 1); }
char *rt_fmt2(char *buf, double v) { return fmtfix(buf, v, 2); }

char *rt_fmtint(char *buf, long v)
{
    char tmp[24];
    int n, i, j = 0, neg = v < 0;
    if (neg) v = -v;
    wsprintfA(tmp, "%ld", v);
    n = rt_len(tmp);
    if (neg) buf[j++] = '-';
    for (i = 0; i < n; i++) {
        buf[j++] = tmp[i];
        if ((n - i - 1) % 3 == 0 && i != n - 1) buf[j++] = ',';
    }
    buf[j] = 0;
    return buf;
}

static DWORD g_seed = 12345;
void rt_srand(DWORD seed) { g_seed = seed ? seed : 1; }
double rt_rand(void)
{
    g_seed = g_seed * 1103515245u + 12345u;
    return (double)((g_seed >> 8) & 0xFFFF) / 65535.0;
}
