#ifndef STRING_H
#define STRING_H
#include "types.h"

void *memcpy(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
void *memmove(void *dst, const void *src, size_t n);

void strcpy(char *dest, const char *src);

char* strncpy(char* dest, const char* src, size_t n);
size_t strlen(char *str);
char* strconcat(char *out, const char *fmt, ...);
char* strrchr(const char* s, int c);
char* itoa(int num, char* str, int base);
int atoi(const char *str);
long strtol(const char *nptr, char **endptr, int base);

char* strstr(const char* haystack, const char* needle);
char* strformat(const char *fmt, ...);

int atoi(const char *str);
int int_to_str(int value, char* out);
int uint_to_str(unsigned int value, char* out);
int i64_to_str(long long value, char* out);
int u64_to_str(unsigned long long value, char* out) ;
int hex32_to_str(uint32_t value, char* out, int width);
int hex64_to_str(unsigned long long value, char* out, int width);

int strcmp(const char* s1, const char* s2);

#endif