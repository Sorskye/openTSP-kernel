#ifndef RAMFS_IMAGE_H
#define RAMFS_IMAGE_H

#include <stdint.h>

#define RAMFS_MAGIC 0x52414653  // 'RAFS'

typedef enum {
    RAMFS_INODE_FILE = 1,
    RAMFS_INODE_DIR  = 2,
} ramfs_inode_type_t;

typedef struct {
    uint32_t magic;
    uint32_t inode_count;
    uint32_t root_inode;   // index of root inode in the inode table
} __attribute__((packed)) ramfs_header_t;

typedef struct {
    uint32_t type;         // ramfs_inode_type_t
    uint32_t parent;       // parent inode index, or UINT32_MAX for root
    char     name[32];     // same limit as your dirent_t
    uint32_t first_child;  // for dirs: index of first child in inode table
    uint32_t child_count;  // for dirs: number of children
    uint32_t data_offset;  // for files: offset into data area (bytes from start of data section)
    uint32_t size;         // for files: size in bytes
} __attribute__((packed)) ramfs_disk_inode_t;

#endif
