#include "string.h"
#include "types.h"
#include "serial.h"

void *memset(void *dst, int c, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    uint8_t v = (uint8_t)c;

    // Byte-wise until aligned or done
    while (n && ((uintptr_t)d & 3)) {
        *d++ = v;
        --n;
    }

    // 32-bit fill
    if (n >= 4) {
        uint32_t v32 = (uint32_t)v;
        v32 |= v32 << 8;
        v32 |= v32 << 16;

        uint32_t *d32 = (uint32_t *)d;
        while (n >= 4) {
            *d32++ = v32;
            n -= 4;
        }
        d = (uint8_t *)d32;
    }

    // Tail bytes
    while (n--) {
        *d++ = v;
    }

    return dst;
}

void *memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;

    // memcpy assumes no overlap

    // Align destination (and source must match alignment to use word copy)
    while (n && (((uintptr_t)d & 3) || ((uintptr_t)s & 3))) {
        *d++ = *s++;
        --n;
    }

    // 32-bit copy
    if (n >= 4) {
        uint32_t *d32 = (uint32_t *)d;
        const uint32_t *s32 = (const uint32_t *)s;

        while (n >= 4) {
            *d32++ = *s32++;
            n -= 4;
        }

        d = (uint8_t *)d32;
        s = (const uint8_t *)s32;
    }

    // Tail bytes
    while (n--) {
        *d++ = *s++;
    }

    return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;

    if (d == s || n == 0) {
        return dst;
    }

    if (d < s) {
        // Forward copy (like memcpy)
        return memcpy(dst, src, n);
    } else {
        // Backward copy to handle overlap safely
        d += n;
        s += n;

        // Byte-wise until aligned
        while (n && (((uintptr_t)d & 3) || ((uintptr_t)s & 3))) {
            --d;
            --s;
            *d = *s;
            --n;
        }

        // 32-bit backward copy
        if (n >= 4) {
            uint32_t *d32 = (uint32_t *)d;
            const uint32_t *s32 = (const uint32_t *)s;

            while (n >= 4) {
                --d32;
                --s32;
                *d32 = *s32;
                n -= 4;
            }

            d = (uint8_t *)d32;
            s = (const uint8_t *)s32;
        }

        // Tail bytes backward
        while (n--) {
            --d;
            --s;
            *d = *s;
        }

        return dst;
    }
}


size_t strlen(char *str) {
    size_t length = 0;
    while (str[length] != '\0') {
        length++;
    }
    return length;
}



void strcpy(char *dest, const char *src) {

    size_t i;
    for (i = 0; src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    dest[i] = '\0';  
}

void strcpyr(char *dest, const char *src) {

    size_t i;
    for (i = 0; src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
}

char* strncpy(char* dest, const char* src, size_t n) {
    char* d = dest;

    while (n && (*d++ = *src++)) {
        n--;
    }

    while (n--) {
        *d++ = '\0';
    }

    return dest;
}



static char *write_int(char *out, long long v) {
    char buf[32];
    int pos = 0;

    if (v < 0) {
        *out++ = '-';
        v = -v;
    }

    if (v == 0) {
        *out++ = '0';
        return out;
    }

    while (v > 0) {
        buf[pos++] = '0' + (v % 10);
        v /= 10;
    }

    while (pos--) {
        *out++ = buf[pos];
    }

    return out;
}

static char *write_hex(char *out, unsigned long long v, int width) {
    char buf[32];
    int pos = 0;

    if (v == 0 && width == 0) {
        *out++ = '0';
        return out;
    }

    while (v > 0) {
        unsigned digit = v & 0xF;
        buf[pos++] = (digit < 10) ? ('0' + digit) : ('a' + (digit - 10));
        v >>= 4;
    }

    while (pos < width) {
        *out++ = '0';
        width--;
    }

    while (pos--) {
        *out++ = buf[pos];
    }

    return out;
}

static char *write_str(char *out, const char *s) {
    while (*s) {
        *out++ = *s++;
    }
    return out;
}


int int_to_str(int value, char* out) {
    char buf[16];
    int i = 0, j = 0;

    if (value == 0) {
        out[0] = '0';
        return 1;
    }

    int neg = value < 0;
    if (neg) value = -value;

    while (value > 0) {
        buf[i++] = '0' + (value % 10);
        value /= 10;
    }

    if (neg) buf[i++] = '-';

    // reverse
    while (i > 0) {
        out[j++] = buf[--i];
    }

    return j;
}

long strtol(const char *nptr, char **endptr, int base)
{
    const char *s = nptr;
    int neg = 0;

    /* skip leading whitespace */
    while (*s == ' ' || *s == '\t' || *s == '\n' ||
           *s == '\v' || *s == '\f' || *s == '\r') {
        s++;
    }

    /* optional sign */
    if (*s == '+') {
        s++;
    } else if (*s == '-') {
        neg = 1;
        s++;
    }

    /* base detection: "0x"/"0X" -> 16, leading "0" -> 8, else 10 */
    if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
        base = 16;
    } else if (base == 0) {
        base = (s[0] == '0') ? 8 : 10;
    }

    if (base < 2 || base > 36) {
        if (endptr) *endptr = (char *)nptr;
        return 0;
    }

    /* magnitude bound: LONG_MAX+1 == |LONG_MIN| on two's complement */
    unsigned long cutoff = neg ? (unsigned long)LONG_MAX + 1UL
                                : (unsigned long)LONG_MAX;
    unsigned long cutlim = cutoff % (unsigned long)base;
    cutoff /= (unsigned long)base;

    unsigned long acc = 0;
    int any = 0;
    int overflow = 0;

    for (;; s++) {
        char c = *s;
        int digit;

        if (c >= '0' && c <= '9') {
            digit = c - '0';
        } else if (c >= 'a' && c <= 'z') {
            digit = c - 'a' + 10;
        } else if (c >= 'A' && c <= 'Z') {
            digit = c - 'A' + 10;
        } else {
            break;
        }

        if (digit >= base) {
            break;
        }

        any = 1;

        if (acc > cutoff || (acc == cutoff && (unsigned long)digit > cutlim)) {
            overflow = 1;
        } else {
            acc = acc * (unsigned long)base + (unsigned long)digit;
        }
    }

    long result;
    if (overflow) {
        result = neg ? LONG_MIN : LONG_MAX;
    } else {
        result = neg ? -(long)acc : (long)acc;
    }

    if (endptr) {
        *endptr = any ? (char *)s : (char *)nptr;
    }

    return result;
}

int atoi(const char *str)
{
    int i = 0;
    int sign = 1;
    int result = 0;

    while (str[i] == ' ') {
        i++;
    }

    if (str[i] == '-') {
        sign = -1;
        i++;
    } else if (str[i] == '+') {
        i++;
    }

    while (str[i] >= '0' && str[i] <= '9') {
        int digit = str[i] - '0';

        if (result < (INT_MIN + digit) / 10) {
            return sign == 1 ? INT_MAX : INT_MIN;
        }

        result = result * 10 - digit;
        i++;
    }

    if (sign == 1 && result == INT_MIN) {
        return INT_MAX;   // handles the exact INT_MAX+1 boundary
    }

    return sign == 1 ? -result : result;
    
}

int uint_to_str(unsigned int value, char* out) {
    char buf[16];
    int i = 0, j = 0;

    if (value == 0) {
        out[0] = '0';
        return 1;
    }

    while (value > 0) {
        buf[i++] = '0' + (value % 10);
        value /= 10;
    }

    while (i > 0) {
        out[j++] = buf[--i];
    }

    return j;
}

int i64_to_str(long long value, char* out) {
    char buf[32];
    int i = 0, j = 0;

    if (value == 0) {
        out[0] = '0';
        return 1;
    }

    int neg = value < 0;
    if (neg) value = -value;

    while (value > 0) {
        buf[i++] = '0' + (value % 10);
        value /= 10;
    }

    if (neg) buf[i++] = '-';

    while (i > 0) {
        out[j++] = buf[--i];
    }

    return j;
}

int u64_to_str(unsigned long long value, char* out) {
    char buf[32];
    int i = 0, j = 0;

    if (value == 0) {
        out[0] = '0';
        return 1;
    }

    while (value > 0) {
        buf[i++] = '0' + (value % 10);
        value /= 10;
    }

    while (i > 0) {
        out[j++] = buf[--i];
    }

    return j;
}

int hex32_to_str(uint32_t value, char* out, int width) {
    char buf[16];
    int i = 0, j = 0;

    if (value == 0) {
        buf[i++] = '0';
    } else {
        while (value > 0) {
            int digit = value & 0xF;
            buf[i++] = (digit < 10) ? ('0' + digit) : ('a' + digit - 10);
            value >>= 4;
        }
    }

    while (i < width) {
        buf[i++] = '0';
    }

    while (i > 0) {
        out[j++] = buf[--i];
    }

    return j;
}

int hex64_to_str(unsigned long long value, char* out, int width) {
    char buf[32];
    int i = 0, j = 0;

    if (value == 0) {
        buf[i++] = '0';
    } else {
        while (value > 0) {
            int digit = value & 0xF;
            buf[i++] = (digit < 10) ? ('0' + digit) : ('a' + digit - 10);
            value >>= 4;
        }
    }

    while (i < width) {
        buf[i++] = '0';
    }

    while (i > 0) {
        out[j++] = buf[--i];
    }

    return j;
}



char *strconcat(char *out, const char *fmt, ...) {
    char *start = out;
    va_list args;
    va_start(args, fmt);

    for (size_t i = 0; fmt[i] != '\0'; ++i) {
        if (fmt[i] != '%') {
            *out++ = fmt[i];
            continue;
        }

        i++;

        /* width: %0NNN */
        int width = 0;
        if (fmt[i] == '0') {
            i++;
            while (fmt[i] >= '0' && fmt[i] <= '9') {
                width = width * 10 + (fmt[i] - '0');
                i++;
            }
        }

        /* %ll */
        int is_ll = 0;
        if (fmt[i] == 'l' && fmt[i+1] == 'l') {
            is_ll = 1;
            i += 2;
        }

        switch (fmt[i]) {
            case 'd': {
                if (is_ll)
                    out = write_int(out, va_arg(args, long long));
                else
                    out = write_int(out, va_arg(args, int));
                break;
            }

            case 'u': {
               // if (is_ll)
                   // out = write_uint(out, va_arg(args, unsigned long long));
               // else
                  //  out = write_uint(out, va_arg(args, unsigned int));
                break;
            }

            case 'x': {
                if (is_ll)
                    out = write_hex(out, va_arg(args, unsigned long long), width);
                else
                    out = write_hex(out, va_arg(args, uint32_t), width);
                break;
            }

            case 's': {
                out = write_str(out, va_arg(args, const char*));
                break;
            }

            case 'c': {
                *out++ = (char)va_arg(args, int);
                break;
            }

            case '%': {
                *out++ = '%';
                break;
            }

            default: {
                *out++ = '%';
                *out++ = fmt[i];
                break;
            }
        }
    }

    *out = '\0';
    va_end(args);
    return start;
}

char* strstr(const char* haystack, const char* needle) {
    if (!*needle) {
        return (char*)haystack;
    }

    for (; *haystack; haystack++) {
        const char* h = haystack;
        const char* n = needle;

        while (*h && *n && (*h == *n)) {
            h++;
            n++;
        }

        if (!*n) {
            return (char*)haystack; 
        }
    }
    
    return NULL; 
}

void reverse(char str[], int length)
{
    int start = 0;
    int end = length - 1;
    while (start < end) {
        char temp = str[start];
        str[start] = str[end];
        str[end] = temp;
        end--;
        start++;
    }
}

char* itoa(int num, char* str, int base)
{
    int i = 0;
    bool isNegative = false;

    /* Handle 0 explicitly, otherwise empty string is
     * printed for 0 */
    if (num == 0) {
        str[i++] = '0';
        str[i] = '\0';
       
    }

    // In standard itoa(), negative numbers are handled
    // only with base 10. Otherwise numbers are
    // considered unsigned.
    if (num < 0 && base == 10) {
        isNegative = true;
        num = -num;
    }

    // Process individual digits
    while (num != 0) {
        int rem = num % base;
        str[i++] = (rem > 9) ? (rem - 10) + 'a' : rem + '0';
        num = num / base;
    }

    // If number is negative, append '-'
    if (isNegative)
        str[i++] = '-';

    str[i] = '\0'; // Append string terminator

    // Reverse the string
    reverse(str, i);

    return str;
}

int strcmp(const char* s1, const char* s2){
    while(*s1 && (*s1 == *s2))
    {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

char* strrchr(const char* s, int c) {
    const char* last = NULL;

    do {
        if (*s == (char)c) {
            last = s;
        }
    } while (*s++);

    return (char*)last;
}
