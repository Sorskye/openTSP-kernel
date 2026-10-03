#ifndef FSIMG_H
#define FSIMG_H


#define FSIMG_MAX_ENTRIES 32
#define FSIMG_FILE_CAPACITY 4096

struct inode* set_tmpfs_from_fsimg(void* image_start, size_t image_size);

typedef struct {
    char name[32];
    struct inode* inode;
} fsimg_entry;

typedef struct {
    fsimg_entry entries[FSIMG_MAX_ENTRIES];
    int count;
} fsimg_dir;

typedef struct {
    uint8_t* data;
    size_t size;
    size_t capacity;
} fsimg_file;

// image specifications

#define FSIMG_MAGIC 0x52414653  // 'RAFS'

typedef enum {
    FSIMG_INODE_FILE = 1,
    FSIMG_INODE_DIR  = 2,
} fsimg_inode_type_t;

typedef struct {
    uint32_t magic;
    uint32_t inode_count;
    uint32_t root_inode;   // index of root inode in the inode table
} __attribute__((packed)) fsimg_header_t;

typedef struct {
    uint32_t type;         // fsimg_inode_type_t
    uint32_t parent;       // parent inode index, or UINT32_MAX for root
    char     name[32];     // same limit as your dirent_t
    uint32_t first_child;  // for dirs: index of first child in inode table
    uint32_t child_count;  // for dirs: number of children
    uint32_t data_offset;  // for files: offset into data area (bytes from start of data section)
    uint32_t size;         // for files: size in bytes
} __attribute__((packed)) fsimg_inode_t;


#endif