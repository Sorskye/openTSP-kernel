#pragma once
#ifndef STDIO_H
#define STDIO_H

#include "types.h"
void printf(const char *fmt, ...);

int max(int a, int b);

int fopen(const char* path, size_t pathlen, char* flags);
void fclose(int fd);

char* fread(int fd, size_t* out_size);
char* fgets(int fd, char* buf, size_t max_len);

#endif