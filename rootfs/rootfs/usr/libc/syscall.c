#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>

#define SYS_READ 0
#define SYS_WRITE 1
#define SYS_OPEN 2
#define SYS_CLOSE 3
#define SYS_GETPID 4
#define SYS_LSEEK 8
#define SYS_BRK 12
#define SYS_YIELD 24
#define SYS_EXIT 60

/* New syscall numbers (follow the x86_64 Linux numbering like the rest).
 * Change these if your kernel uses different ones. */
#define SYS_FORK 57     /* reserved: not used yet, fork() is a placeholder   */
#define SYS_EXECVE 59   /* reserved: not used yet, execve() is a placeholder */
#define SYS_WAIT4 61    /* reserved: not used yet, wait*() are placeholders  */
#define SYS_GETCWD 79   /* implemented: getcwd(buf, size)                    */
#define SYS_CHDIR 80    /* implemented: chdir(path)                          */

/* Used by getcwd(NULL, 0) when we have to allocate the buffer ourselves. */
#define CWD_DEFAULT_SIZE 256

static int syscall3(uint32_t number, uint32_t arg0, uint32_t arg1, uint32_t arg2)
{
  int result;

  __asm__ volatile ("int $0x80"
    : "=a"(result)
    : "a"(number), "b"(arg0), "c"(arg1), "d"(arg2)
    : "cc", "memory");

  return result;
}

static int syscall2(uint32_t number, uint32_t arg0, uint32_t arg1)
{
  int result;

  __asm__ volatile ("int $0x80"
    : "=a"(result)
    : "a"(number), "b"(arg0), "c"(arg1)
    : "cc", "memory");

  return result;
}

static int syscall1(uint32_t number, uint32_t arg0)
{
  int result;

  __asm__ volatile ("int $0x80"
    : "=a"(result)
    : "a"(number), "b"(arg0)
    : "cc", "memory");

  return result;
}

static int syscall0(uint32_t number)
{
  int result;

  __asm__ volatile ("int $0x80"
    : "=a"(result)
    : "a"(number)
    : "cc", "memory");

  return result;
}

static int syscall_error(int result)
{
  if (result < 0) {
    errno = EIO;
    return -1;
  }
  return result;
}

int read(int fd, void *buffer, size_t size)
{
  return syscall_error(syscall3(SYS_READ, (uint32_t)fd,
                  (uint32_t)(uintptr_t)buffer, (uint32_t)size));
}

int write(int fd, const void *buffer, size_t size)
{
  return syscall_error(syscall3(SYS_WRITE, (uint32_t)fd,
                  (uint32_t)(uintptr_t)buffer, (uint32_t)size));
}

int open(const char *path, int flags, ...)
{
  return syscall_error(syscall3(SYS_OPEN, (uint32_t)(uintptr_t)path,
                  (uint32_t)flags, 0));
}

int close(int fd)
{
  return syscall_error(syscall1(SYS_CLOSE, (uint32_t)fd));
}

off_t lseek(int fd, off_t offset, int whence)
{
  return syscall_error(syscall3(SYS_LSEEK, (uint32_t)fd,
                  (uint32_t)offset, (uint32_t)whence));
}

int fstat(int fd, struct stat *status)
{
  if (!status) {
    errno = EINVAL;
    return -1;
  }
  memset(status, 0, sizeof(*status));
  status->st_mode = fd >= 0 && fd <= 2 ? S_IFCHR : S_IFREG;
  status->st_size = 0;
  status->st_blksize = 4096;
  return 0;
}

int isatty(int fd)
{
  return fd >= 0 && fd <= 2;
}

int getpid(void)
{
  return syscall0(SYS_GETPID);
}

int kill(int pid, int signal)
{
  (void)pid;
  (void)signal;
  errno = ENOSYS;
  return -1;
}

void *sbrk(ptrdiff_t increment)
{
  static uintptr_t current_break;
  uintptr_t requested_break;
  int result;

  if (current_break == 0) {
    extern char _end;
    current_break = (uintptr_t)&_end;
  }

  if ((increment > 0 && current_break > UINT32_MAX - (uintptr_t)increment) ||
    (increment < 0 && current_break < (uintptr_t)(-(increment + 1)) + 1)) {
    errno = ENOMEM;
    return (void *)-1;
  }

  requested_break = current_break + increment;
  result = syscall1(SYS_BRK, (uint32_t)requested_break);
  if (result < 0 || (uintptr_t)result != requested_break) {
    errno = ENOMEM;
    return (void *)-1;
  }

  current_break = requested_break;
  return (void *)(current_break - increment);
}

void _exit(int status)
{
  syscall1(SYS_EXIT, (uint32_t)status);
  for (;;)
    __asm__ volatile ("pause");
}

int unlink(const char *path)
{
  (void)path;
  errno = ENOSYS;
  return -1;
}

int link(const char *old_path, const char *new_path)
{
  (void)old_path;
  (void)new_path;
  errno = ENOSYS;
  return -1;
}

/* ---- Process control (placeholders) ---------------------------------- */

int fork(void)
{
  errno = ENOSYS;
  return -1;
}

int execve(const char *path, char *const argv[], char *const envp[])
{
  (void)path;
  (void)argv;
  (void)envp;
  errno = ENOSYS;
  return -1;
}

int wait(int *status)
{
  (void)status;
  errno = ECHILD;
  return -1;
}

int waitpid(int pid, int *status, int options)
{
  (void)pid;
  (void)status;
  (void)options;
  errno = ECHILD;
  return -1;
}

/* ---- Working directory (implemented) ---------------------------------- */

/*
 * Kernel contract:
 *   SYS_CHDIR(path)         -> 0 on success, negative on error
 *   SYS_GETCWD(buf, size)   -> >= 0 on success (buf holds a NUL-terminated
 *                              absolute path), negative on error.
 *                              Return -ERANGE if the path doesn't fit.
 */
int chdir(const char *path)
{
  if (!path) {
    errno = EFAULT;
    return -1;
  }
  return syscall_error(syscall1(SYS_CHDIR, (uint32_t)(uintptr_t)path));
}

char *getcwd(char *buffer, size_t size)
{
  char *allocated = NULL;
  int result;

  if (!buffer) {
    /* POSIX extension: allocate the buffer for the caller. */
    if (size == 0)
      size = CWD_DEFAULT_SIZE;
    allocated = malloc(size);
    if (!allocated) {
      errno = ENOMEM;
      return NULL;
    }
    buffer = allocated;
  } else if (size == 0) {
    errno = EINVAL;
    return NULL;
  }

  result = syscall2(SYS_GETCWD, (uint32_t)(uintptr_t)buffer, (uint32_t)size);
  if (result < 0) {
    free(allocated);
    errno = (result == -ERANGE) ? ERANGE : EIO;
    return NULL;
  }

  return buffer;
}

/* ---- Newlib reentrant stubs ------------------------------------------- */

int _read(int fd, void *buffer, size_t size) { return read(fd, buffer, size); }
int _write(int fd, const void *buffer, size_t size) { return write(fd, buffer, size); }
int _open(const char *path, int flags, int mode) { return open(path, flags, mode); }
int _close(int fd) { return close(fd); }
off_t _lseek(int fd, off_t offset, int whence) { return lseek(fd, offset, whence); }
int _fstat(int fd, struct stat *status) { return fstat(fd, status); }
int _isatty(int fd) { return isatty(fd); }
int _getpid(void) { return getpid(); }
int _kill(int pid, int signal) { return kill(pid, signal); }
void *_sbrk(ptrdiff_t increment) { return sbrk(increment); }
int _unlink(const char *path) { return unlink(path); }
int _link(const char *old_path, const char *new_path) { return link(old_path, new_path); }
int _fork(void) { return fork(); }
int _execve(const char *path, char *const argv[], char *const envp[])
{
  return execve(path, argv, envp);
}
int _wait(int *status) { return wait(status); }
