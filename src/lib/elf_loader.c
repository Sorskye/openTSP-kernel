#include "types.h"
#include "string.h"
#include "elf.h"
#include "elf_loader.h"
#include "vfs.h"
#include "vmm.h"
#include "pmm.h"

/* ============================================================================
 * Hooks -- rename/adjust these to match what already exists in your kernel.
 * Nothing below this block should need to change.
 * ============================================================================
 */

/* --- VFS --- */
#ifndef SEEK_SET
#define SEEK_SET 0
#endif
#ifndef O_RDONLY
#define O_RDONLY 0
#endif



/* PAGE_PRESENT / PAGE_WRITABLE / PAGE_USER / PAGE_FRAME_MASK / PAGE_SIZE
 * all come from vmm.h, included above via elf_loader.h. vm_map_page ORs in
 * PAGE_PRESENT itself at the PTE level, so callers here never pass it. */

/* ============================================================================
 * Loader
 * ============================================================================
 */

#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif
#define PAGE_ALIGN_DOWN(x) ((x) & ~(PAGE_SIZE - 1))
#define PAGE_ALIGN_UP(x)   (((x) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))

static int elf32_validate(const Elf32_Ehdr *eh)
{
    if (!elf32_check_magic(eh))                    return 0;
    if (eh->e_ident[EI_CLASS] != ELFCLASS32)        return 0;
    if (eh->e_ident[EI_DATA]  != ELFDATA2LSB)       return 0;
    if (eh->e_type            != ET_EXEC)           return 0; /* static exec only, for now */
    if (eh->e_machine         != EM_386)            return 0;
    if (eh->e_phnum           == 0)                 return 0;
    if (eh->e_phentsize       != sizeof(Elf32_Phdr))return 0;
    return 1;
}

/*
 * Maps and populates every page touched by one PT_LOAD segment.
 *
 * A page is zeroed the first time it's allocated -- that's what gives you
 * correct .bss for free (anything past p_filesz up to p_memsz just never
 * gets a file copy, so it stays zero). If two segments happen to share a
 * page (common when .text and .data are close together) we detect the
 * existing mapping and reuse that frame instead of re-allocating and
 * stomping the first segment's data.
 *
 * Known simplification: if a page is shared between segments, its
 * protection flags are whatever the *first* segment to touch it asked for.
 * A later RW segment sharing a page with an earlier RO one won't upgrade
 * it. Fine for hobby-OS purposes; real loaders handle this with per-page
 * flag unions if you ever care.
 */
static int load_segment(struct file* file, const Elf32_Phdr *ph, vm_env_t *vm_env)
{
    uint32_t seg_start    = ph->p_vaddr;
    uint32_t seg_file_end = ph->p_vaddr + ph->p_filesz;
    uint32_t seg_mem_end  = ph->p_vaddr + ph->p_memsz;

    uint32_t page_start = PAGE_ALIGN_DOWN(seg_start);
    uint32_t page_end   = PAGE_ALIGN_UP(seg_mem_end);

    uint32_t flags = PAGE_USER;
    if (ph->p_flags & PF_W) flags |= PAGE_WRITABLE;

    for (uint32_t vaddr = page_start; vaddr < page_end; vaddr += PAGE_SIZE) {

        uint32_t phys = vmm_get_phys(vm_env, vaddr);
        int fresh = 0;

        if (!phys) {
            uint32_t *frame = phys_alloc_page();
            if (!frame) return -1;
            phys = (uint32_t)(uintptr_t)frame;

            vm_map_page(vm_env, vaddr, phys, flags);

            fresh = 1;
        }

        uint8_t *win = (uint8_t *)vmm_map_temp(phys);

        if (fresh)
            memset(win, 0, PAGE_SIZE);

        /* copy whatever part of the file lands in this page */
        uint32_t copy_start = seg_start    > vaddr             ? seg_start    : vaddr;
        uint32_t copy_end   = seg_file_end < vaddr + PAGE_SIZE ? seg_file_end : vaddr + PAGE_SIZE;

        if (copy_end > copy_start) {
            uint32_t file_off = ph->p_offset + (copy_start - seg_start);
            uint32_t win_off  = copy_start - vaddr;
            uint32_t len      = copy_end - copy_start;

            if (vfs_lseek(file, file_off, SEEK_SET) < 0)         { vmm_unmap_temp(); return -1; }
            if (vfs_read(file, win + win_off, len) != (int)len) { vmm_unmap_temp(); return -1; }
        }
        /* anything in [copy_end, vaddr+PAGE_SIZE) within seg_mem_end is .bss --
         * already zero from the fresh-page memset above, nothing to do. */

        vmm_unmap_temp();
    }

    return 0;
}

int elf_load(const char *path, vm_env_t *vm_env, elf_load_result_t *out)
{
    memset(out, 0, sizeof(*out));

    struct file* file = vfs_open(path, O_RDONLY);
    if (file == NULL) return -1;

    Elf32_Ehdr ehdr;
    if (vfs_read(file, &ehdr, sizeof(ehdr)) != sizeof(ehdr)) {
        vfs_close(file);
        return -1;
    }

    if (!elf32_validate(&ehdr)) {
        vfs_close(file);
        return -1;
    }

    uint32_t highest = 0;

    for (uint16_t i = 0; i < ehdr.e_phnum; i++) {
        Elf32_Phdr phdr;

        if (vfs_lseek(file, ehdr.e_phoff + (uint32_t)i * ehdr.e_phentsize, SEEK_SET) < 0) {
            vfs_close(file);
            return -1;
        }
        if (vfs_read(file, &phdr, sizeof(phdr)) != sizeof(phdr)) {
            vfs_close(file);
            return -1;
        }

        if (phdr.p_type != PT_LOAD || phdr.p_memsz == 0)
            continue;

        if (load_segment(file, &phdr, vm_env) != 0) {
            vfs_close(file);
            return -1;   /* caller destroys the process; its normal teardown path
                             frees whatever frames already got mapped */
        }

        uint32_t seg_end = phdr.p_vaddr + phdr.p_memsz;
        if (seg_end > highest) highest = seg_end;
    }

    vfs_close(file);

    out->entry      = ehdr.e_entry;
    out->heap_start = PAGE_ALIGN_UP(highest);
    out->valid      = 1;
    return 0;
}