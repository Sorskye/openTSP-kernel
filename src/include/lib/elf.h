#ifndef ELF_H
#define ELF_H

#include "types.h"

/* ---- e_ident indices ---- */
#define EI_MAG0       0
#define EI_MAG1       1
#define EI_MAG2       2
#define EI_MAG3       3
#define EI_CLASS      4
#define EI_DATA       5
#define EI_VERSION    6
#define EI_OSABI      7
#define EI_ABIVERSION 8
#define EI_PAD        9
#define EI_NIDENT     16

#define ELFMAG0 0x7F
#define ELFMAG1 'E'
#define ELFMAG2 'L'
#define ELFMAG3 'F'

#define ELFCLASSNONE 0
#define ELFCLASS32   1
#define ELFCLASS64   2

#define ELFDATANONE 0
#define ELFDATA2LSB 1   /* little endian -- what x86 wants */
#define ELFDATA2MSB 2

/* ---- e_type ---- */
#define ET_NONE 0
#define ET_REL  1
#define ET_EXEC 2   /* the only kind the loader below accepts: static, non-relocatable */
#define ET_DYN  3
#define ET_CORE 4

/* ---- e_machine ---- */
#define EM_386 3

#define EV_CURRENT 1

typedef struct {
    unsigned char e_ident[EI_NIDENT];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;      /* virtual address of first instruction */
    uint32_t e_phoff;      /* program header table file offset */
    uint32_t e_shoff;      /* section header table file offset (unused by loader) */
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed)) Elf32_Ehdr;

/* ---- p_type ---- */
#define PT_NULL    0
#define PT_LOAD    1   /* the only one the loader actually cares about */
#define PT_DYNAMIC 2
#define PT_INTERP  3
#define PT_NOTE    4
#define PT_SHLIB   5
#define PT_PHDR    6

/* ---- p_flags ---- */
#define PF_X 0x1
#define PF_W 0x2
#define PF_R 0x4

typedef struct {
    uint32_t p_type;
    uint32_t p_offset;    /* offset of segment in file */
    uint32_t p_vaddr;     /* virtual address to load segment at */
    uint32_t p_paddr;     /* unused on most hobby OSes */
    uint32_t p_filesz;    /* bytes of segment actually present in the file */
    uint32_t p_memsz;     /* bytes segment occupies in memory (>= p_filesz; the
                              difference is .bss and must be zeroed, not copied) */
    uint32_t p_flags;
    uint32_t p_align;
} __attribute__((packed)) Elf32_Phdr;

static inline int elf32_check_magic(const Elf32_Ehdr *eh)
{
    return eh->e_ident[EI_MAG0] == ELFMAG0 &&
           eh->e_ident[EI_MAG1] == ELFMAG1 &&
           eh->e_ident[EI_MAG2] == ELFMAG2 &&
           eh->e_ident[EI_MAG3] == ELFMAG3;
}

#endif /* ELF_H */