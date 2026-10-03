#pragma once
#ifndef SYSCALL_H
#define SYSCALL_H

#include "types.h"

typedef int (*syscall_fn)(void *arg);

enum {
    SYS_READ = 0,
    SYS_WRITE = 1,
    SYS_OPEN = 2,
    SYS_CLOSE = 3,
    SYS_GETPID = 4,
    
    SYS_LSEEK = 8,

    SYS_BRK = 12,

    SYS_YIELD = 24,

    SYS_EXIT = 60,
    
    SYS_GETCWD = 79,
    SYS_CHDIR = 80,
    
};

/* i386 syscall ABI: eax=number, ebx/ecx/edx/esi/edi=arguments, eax=result. */

#define MAX_SYSCALL 439
typedef struct {
    int (*handler)(uint32_t syscall, uint32_t *returnaddr, uint32_t arg0, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5);
} syscall_handler_t;

extern syscall_handler_t syscall_lookup_table[MAX_SYSCALL];

int syscall_write(int fd, const void *buffer, size_t size);
int syscall_open(const char *filename, int flags);
int syscall_read(int fd, void *buffer, size_t size);
int syscall_close(int fd);
void syscall_exit(int status) __attribute__((noreturn));
void syscall_yield(void);


// returns corrosponding syscall name
char *syscallDefineName(int syscallNum);

syscall_handler_t *alloc_syscall_handler(int(*handler)(uint32_t syscall, uint32_t *returnaddr, uint32_t arg0, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5));
int registerSyscallHandler(uint32_t syscall, syscall_handler_t *handler);
int dispatchSyscall(uint32_t syscall,uint32_t *returnreg, uint32_t arg0, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5);

void regDefSyscallHandlers();

#endif