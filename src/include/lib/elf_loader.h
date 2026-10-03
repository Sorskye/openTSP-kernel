#ifndef ELF_LOADER_H
#define ELF_LOADER_H

#include "types.h"
#include "vmm.h"    /* for vm_env_t and the PAGE_* flags */

typedef struct {
    uint32_t entry;         /* value to put in EIP when you iret into this process */
    uint32_t heap_start;    /* page-aligned address just past the last PT_LOAD segment --
                                a reasonable place to start this process's brk/heap */
    int      valid;
} elf_load_result_t;

/*
 * Loads the ELF executable at `path` (via your VFS) into the address space
 * described by `vm_env`. vm_env->pd_addr must already be a valid, allocated
 * page directory with the kernel's higher-half PDEs copied in.
 *
 * This does NOT switch CR3 and does NOT touch the currently active address
 * space -- it's safe to call for a process that isn't running yet, which is
 * the normal case (you're building it from inside process_spawn()).
 *
 * Returns 0 on success and fills *out. Returns -1 on failure (bad path, bad
 * ELF, out of memory). On failure some frames may already be mapped into
 * vm_env -- don't try to hand-unwind that here, just destroy the process
 * through your normal teardown path, which should already free every frame
 * referenced by its page directory.
 */
int elf_load(const char *path, vm_env_t *vm_env, elf_load_result_t *out);

#endif /* ELF_LOADER_H */