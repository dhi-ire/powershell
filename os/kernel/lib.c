#include "lib.h"

void *memset(void *d, int c, size_t n)
{
    uint8_t *p = d;
    while (n--) *p++ = (uint8_t)c;
    return d;
}

void memset32(uint32_t *d, uint32_t v, size_t count)
{
    __asm__ volatile("rep stosl" : "+D"(d), "+c"(count) : "a"(v) : "memory");
}

void *memcpy(void *d, const void *s, size_t n)
{
    void *ret = d;
    size_t words = n / 4, rest = n % 4;
    __asm__ volatile("rep movsl" : "+D"(d), "+S"(s), "+c"(words) :: "memory");
    __asm__ volatile("rep movsb" : "+D"(d), "+S"(s), "+c"(rest) :: "memory");
    return ret;
}

void *memmove(void *d, const void *s, size_t n)
{
    uint8_t *dp = d;
    const uint8_t *sp = s;
    if (dp == sp || n == 0) return d;
    if (dp < sp || dp >= sp + n) return memcpy(d, s, n);
    while (n--) dp[n] = sp[n];
    return d;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const uint8_t *x = a, *y = b;
    for (; n; n--, x++, y++)
        if (*x != *y) return *x - *y;
    return 0;
}

size_t strlen(const char *s)
{
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) a++, b++;
    return (uint8_t)*a - (uint8_t)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    for (; n; n--, a++, b++) {
        if (*a != *b) return (uint8_t)*a - (uint8_t)*b;
        if (!*a) return 0;
    }
    return 0;
}

char *strcpy(char *d, const char *s)
{
    char *r = d;
    while ((*d++ = *s++)) ;
    return r;
}

char *strncpy(char *d, const char *s, size_t n)
{
    size_t i = 0;
    for (; i < n && s[i]; i++) d[i] = s[i];
    for (; i < n; i++) d[i] = 0;
    return d;
}

/* Always NUL-terminates (when n > 0). */
void strlcpy(char *d, const char *s, size_t n)
{
    if (!n) return;
    size_t i = 0;
    for (; i + 1 < n && s[i]; i++) d[i] = s[i];
    d[i] = 0;
}

char *strcat(char *d, const char *s)
{
    strcpy(d + strlen(d), s);
    return d;
}

char *strchr(const char *s, int c)
{
    for (; *s; s++)
        if (*s == (char)c) return (char *)s;
    return c ? NULL : (char *)s;
}

char *strrchr(const char *s, int c)
{
    const char *last = NULL;
    for (; *s; s++)
        if (*s == (char)c) last = s;
    return (char *)(c ? last : s);
}

int isdigit(int c) { return c >= '0' && c <= '9'; }
int isspace(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
int toupper(int c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }

int atoi(const char *s)
{
    int sign = 1, v = 0;
    while (isspace(*s)) s++;
    if (*s == '-') sign = -1, s++;
    else if (*s == '+') s++;
    while (isdigit(*s)) v = v * 10 + (*s++ - '0');
    return v * sign;
}

/* Minimal printf: %d %i %u %x %X %s %c %p %%, with width, '0' and '-' flags. */
int vsnprintf(char *buf, size_t n, const char *fmt, va_list ap)
{
    size_t o = 0;
#define PUT(ch) do { if (o + 1 < n) buf[o] = (ch); o++; } while (0)
    for (; *fmt; fmt++) {
        if (*fmt != '%') { PUT(*fmt); continue; }
        fmt++;
        int left = 0, zero = 0, width = 0;
        for (;; fmt++) {
            if (*fmt == '-') left = 1;
            else if (*fmt == '0') zero = 1;
            else break;
        }
        while (isdigit(*fmt)) width = width * 10 + (*fmt++ - '0');

        char tmp[16];
        const char *s = tmp;
        int len = 0;
        switch (*fmt) {
        case 'd': case 'i': case 'u': case 'x': case 'X': case 'p': {
            uint32_t v;
            int neg = 0, base = (*fmt == 'x' || *fmt == 'X' || *fmt == 'p') ? 16 : 10;
            if (*fmt == 'd' || *fmt == 'i') {
                int32_t sv = va_arg(ap, int32_t);
                if (sv < 0) { neg = 1; v = (uint32_t)(-(sv + 1)) + 1; } else v = sv;
            } else {
                v = va_arg(ap, uint32_t);
            }
            const char *digits = (*fmt == 'X') ? "0123456789ABCDEF" : "0123456789abcdef";
            char rev[12];
            int r = 0;
            do { rev[r++] = digits[v % base]; v /= base; } while (v);
            if (neg) tmp[len++] = '-';
            while (r) tmp[len++] = rev[--r];
            break;
        }
        case 's':
            s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            len = strlen(s);
            break;
        case 'c':
            tmp[0] = (char)va_arg(ap, int);
            len = 1;
            break;
        case '%':
            tmp[0] = '%';
            len = 1;
            break;
        case 0:
            fmt--;
            continue;
        default:
            tmp[0] = '%'; tmp[1] = *fmt;
            len = 2;
        }
        int pad = width > len ? width - len : 0;
        if (!left) {
            /* zero padding goes after a minus sign */
            if (zero && s == tmp && tmp[0] == '-') { PUT('-'); s++; len--; }
            while (pad--) PUT(zero ? '0' : ' ');
        }
        for (int i = 0; i < len; i++) PUT(s[i]);
        if (left) while (pad--) PUT(' ');
    }
    if (n) buf[o < n ? o : n - 1] = 0;
#undef PUT
    return (int)o;
}

int snprintf(char *buf, size_t n, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(buf, n, fmt, ap);
    va_end(ap);
    return r;
}
