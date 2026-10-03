#include "syscall.h"
#include "IDT.h"
#include "serial.h"


#include "ufs.h"
#include "vmm.h"

#include "task.h"



syscall_handler_t syscall_lookup_table[MAX_SYSCALL];

static inline int syscall0(uint32_t number)
{
    int result;
    __asm__ volatile ("int $0x80"
        : "=a"(result)
        : "a"(number)
        : "cc", "memory");
    return result;
}

static inline int syscall3(uint32_t number, uint32_t arg1, uint32_t arg2, uint32_t arg3)
{
    int result;
    __asm__ volatile ("int $0x80"
        : "=a"(result)
        : "a"(number), "b"(arg1), "c"(arg2), "d"(arg3)
        : "cc", "memory");
    return result;
}


int syscall_write(int fd, const void *buffer, size_t size)
{
    return syscall3(SYS_WRITE, (uint32_t)fd, (uint32_t)buffer, (uint32_t)size);
}

int syscall_open(const char *path, int flags)
{
    return syscall3(SYS_OPEN, (uint32_t)path, (uint32_t)flags,0);
}

int syscall_read(int fd, void *buffer, size_t size)
{
    return syscall3(SYS_READ, (uint32_t)fd, (uint32_t)buffer, (uint32_t)size);
}

int syscall_close(int fd)
{
    return syscall3(SYS_CLOSE, (uint32_t)fd, 0, 0);
}

void syscall_exit(int status)
{
    syscall3(SYS_EXIT, (uint32_t)status, 0, 0);
    
    for (;;) {
        __asm__ volatile ("int $32" : : : "cc", "memory");
    }
}
void syscall_yield(void)
{
    syscall0(SYS_YIELD);
    __asm__ volatile ("int $32" : : : "cc", "memory");
}



int syscallTop = 8;
char *syscallToString[] = {
    "READ",
    "WRITE",
    "OPEN",
    "CLOSE",
};

char *syscallDefineName(int syscallNum){
    if (syscallNum > syscallTop){
        return "NOT_IN_LIST";
    }
        
    return syscallToString[syscallNum];
}

int registerSyscallHandler(uint32_t syscall, syscall_handler_t *handler)
{
    if (syscall >= MAX_SYSCALL)
        return -1;

    if (handler == NULL)
        return -1;

    if (syscall_lookup_table[syscall].handler != NULL)
        return -1;

    syscall_lookup_table[syscall].handler = handler->handler;

    return 0;
}

syscall_handler_t *alloc_syscall_handler(int(*handler)(uint32_t syscall, uint32_t *returnaddr, uint32_t arg0, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5))
{
    syscall_handler_t *new_handler = kmalloc(sizeof(syscall_handler_t));
    if(new_handler == NULL){
        return NULL;
    }

    new_handler->handler = handler;
    return new_handler;
}

syscall_handler_t *syscall_lookup(uint32_t syscall)
{
    if (syscall >= MAX_SYSCALL){
        return NULL;
    }

    if (syscall_lookup_table[syscall].handler == NULL){
        return NULL;
    }

    return &syscall_lookup_table[syscall];
}

int dispatchSyscall(uint32_t syscall,uint32_t *returnreg, uint32_t arg0, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5)
{
    syscall_handler_t *entry = syscall_lookup(syscall);

    if (entry == NULL){
        return -1;
    }

    return entry->handler(syscall, returnreg, arg0, arg1, arg2, arg3, arg4, arg5);
}


void regDefSyscallHandlers()
{
    // SYS_READ HANDLER
    int handle_read(uint32_t syscall, uint32_t *returnaddr, uint32_t arg0, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5){
        *returnaddr = sys_read((int)arg0, (void *)(uintptr_t)arg1, arg2);
        return 0;
    }
    syscall_handler_t *read_handler = alloc_syscall_handler(handle_read);
    if(read_handler == NULL){
        serial_print("read handler alloc is NULL\n");
        return;
    }

    // SYS_WRITE HANDLER
    int handle_write(uint32_t syscall, uint32_t *returnaddr, uint32_t arg0, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5){
        *returnaddr = sys_write((int)arg0, (const void *)(uintptr_t)arg1, arg2);
        return 0;
    }
    syscall_handler_t *write_handler = alloc_syscall_handler(handle_write);
    if(write_handler == NULL){
        serial_print("write handler alloc is NULL\n");
        return;
    }

    // SYS_OPEN HANDLER
    int handle_open(uint32_t syscall, uint32_t *returnaddr, uint32_t arg0, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5){
        *returnaddr = sys_open((const char *)(uintptr_t)arg0, arg1);
        return 0;
    }
    syscall_handler_t *open_handler = alloc_syscall_handler(handle_open);
    if(open_handler == NULL){
        serial_print("open handler alloc is NULL\n");
        return;
    }

    // SYS_CLOSE HANDLER
    int handle_close(uint32_t syscall, uint32_t *returnaddr, uint32_t arg0, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5){
        *returnaddr = sys_close((int)arg0);
        return 0;
    }
    syscall_handler_t *close_handler = alloc_syscall_handler(handle_close);
    if(close_handler == NULL){
        serial_print("close handler alloc is NULL\n");
        return ;
    }

    // SYS_EXIT HANDLER
    int handle_exit(uint32_t syscall, uint32_t *returnaddr, uint32_t arg0, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5){
        if (current_task && current_process) {
            process_exit(current_process);
            trigger_schedule();
        }
        *returnaddr = 0;
        return 0;
    }
    syscall_handler_t *exit_handler = alloc_syscall_handler(handle_exit);
    if(exit_handler == NULL){
        serial_print("exit handler alloc is NULL\n");
        return;
    }

    //SYS_YIELD HANDLER
    int handle_yield(uint32_t syscall, uint32_t *returnaddr, uint32_t arg0, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5){
        if(current_task){
            trigger_schedule();
        }
        *returnaddr = 0;
        return 0;
    }
    syscall_handler_t *yield_handler = alloc_syscall_handler(handle_yield);
    if(yield_handler == NULL){
        serial_print("yield handler alloc is NULL\n");
        return;
    }

    // SYS_LSEEK HANDLER
    int handle_lseek(uint32_t syscall, uint32_t *returnaddr, uint32_t arg0, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5){

        #define MAX_FDS 16
        #define SEEK_SET 0
        #define SEEK_CUR 1
        #define SEEK_END 2
        #define EBADF     9
        #define EINVAL   22
        #define ESPIPE   29
        #define EOVERFLOW 75

        int fd = (int)arg0;
        long offset = (long)arg1;
        int whence = (int)arg2;

        file_t *f;
        int64_t base, new_pos;

        /* 1. Validate the descriptor. */
        if (fd < 0 || fd >= MAX_FDS) {
            *returnaddr = (uint32_t)-EBADF;
            return 0;
        }

        f = current_process->fd_table[fd];
        if (f == NULL) {
            *returnaddr = (uint32_t)-EBADF;
            return 0;
        }

        /* 2. Only regular files are seekable (pipes, terminals, etc. are not). */
        if (f->inode->type != INODE_FILE) {
            *returnaddr = (uint32_t)-ESPIPE;
            return 0;
        }

        /* 3. Pick the base position. */
        switch (whence) {
        case SEEK_SET:
            base = 0;
            break;
        case SEEK_CUR:
            base = f->offset;
            break;
        case SEEK_END:
            base = (int64_t)f->inode->size;
            break;
        default:
            *returnaddr = (uint32_t)-EINVAL;
            return 0;
        }

        /* 4. Compute in 64 bits so we can detect overflow and negatives. */
        new_pos = base + (int64_t)offset;

        if (new_pos < 0) {
            *returnaddr = (uint32_t)-EINVAL;
            return 0;                 /* before start of file */
        }

        if (new_pos > 0x7FFFFFFF) {
            *returnaddr = (uint32_t)-EOVERFLOW;
            return 0;                 /* doesn't fit in a positive 32-bit long */
        }

        /* 5. Commit only after every check has passed. */
        f->offset = (long)new_pos;

        *returnaddr = (uint32_t)new_pos;
        return 0;
    }
    syscall_handler_t *lseek_handler = alloc_syscall_handler(handle_lseek);
    if(lseek_handler == NULL){
        serial_print("yield handler alloc is NULL\n");
        return;
    }

    // SYS_GETPID HANDLER
    int handle_getpid(uint32_t syscall, uint32_t *returnaddr, uint32_t arg0, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5){
        (void)syscall;
        (void)arg0;
        (void)arg1;
        (void)arg2;
        (void)arg3;
        (void)arg4;
        (void)arg5;
        *returnaddr = current_process ? current_process->pid : (uint32_t)-1;
        return 0;
    }
    syscall_handler_t *getpid_handler = alloc_syscall_handler(handle_getpid);
    if(getpid_handler == NULL){
        serial_print("getpid handler alloc is NULL\n");
        return;
    }

    // SYS_BRK HANDLER
    int handle_brk(uint32_t syscall, uint32_t *returnaddr, uint32_t arg0, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5){
        (void)syscall;
        (void)arg1;
        (void)arg2;
        (void)arg3;
        (void)arg4;
        (void)arg5;
        if (!current_process || !current_process->vm_env) {
            *returnaddr = (uint32_t)-1;
            return 0;
        }

        vheap_t *heap = &current_process->vm_env->vheap;
        if (arg0 < heap->start || arg0 > heap->max) {
            *returnaddr = heap->current;
            return 0;
        }

        heap->current = arg0;
        heap->end = arg0;
        *returnaddr = arg0;
        return 0;
    }
    syscall_handler_t *brk_handler = alloc_syscall_handler(handle_brk);
    if(brk_handler == NULL){
        serial_print("brk handler alloc is NULL\n");
        return;
    }

    // SYS_GETCWD HANDLER
    int handle_getcwd(uint32_t syscall, uint32_t *returnaddr, uint32_t arg0, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5){
        (void)syscall;
        (void)arg2;
        (void)arg3;
        (void)arg4;
        (void)arg5;
        *returnaddr = sys_getcwd((char *)(uintptr_t)arg0, arg1);
        return 0;
    }
    syscall_handler_t *getcwd_handler = alloc_syscall_handler(handle_getcwd);
    if(getcwd_handler == NULL){
        serial_print("getcwd handler alloc is NULL\n");
        return;
    }

    // SYS_CHDIR HANDLER
    int handle_chdir(uint32_t syscall, uint32_t *returnaddr, uint32_t arg0, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5){
        (void)syscall;
        (void)arg1;
        (void)arg2;
        (void)arg3;
        (void)arg4;
        (void)arg5;
        *returnaddr = sys_chdir((const char *)(uintptr_t)arg0);
        return 0;
    }
    syscall_handler_t *chdir_handler = alloc_syscall_handler(handle_chdir);
    if(chdir_handler == NULL){
        serial_print("chdir handler alloc is NULL\n");
        return;
    }


    


    registerSyscallHandler(SYS_READ, read_handler);
    registerSyscallHandler(SYS_WRITE, write_handler);
    registerSyscallHandler(SYS_OPEN, open_handler);
    registerSyscallHandler(SYS_CLOSE, close_handler);
    registerSyscallHandler(SYS_EXIT, exit_handler);
    registerSyscallHandler(SYS_YIELD, yield_handler);
    registerSyscallHandler(SYS_LSEEK, lseek_handler);
    registerSyscallHandler(SYS_GETPID, getpid_handler);
    registerSyscallHandler(SYS_BRK, brk_handler);
    registerSyscallHandler(SYS_GETCWD, getcwd_handler);
    registerSyscallHandler(SYS_CHDIR, chdir_handler);
    return;
}