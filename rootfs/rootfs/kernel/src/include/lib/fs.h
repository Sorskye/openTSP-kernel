#ifndef FS_H
#define FS_H
#include "types.h"


#define MAX_FD 16

//inode

typedef enum {
    INODE_FILE,
    INODE_DEV,
    INODE_DIR,
} inode_type_t;

struct inode;

typedef struct dirent {
    char name[32];
    inode_type_t type;
} dirent_t;

struct inode_ops {
    struct inode* (*lookup)(struct inode* dir, const char* name);
    int (*create)(struct inode* dir, const char* name);
    int (*mkdir)(struct inode* dir, const char* name);
    int (*readdir)(struct inode* dir, struct dirent* out, int index);
    int (*unlink)(struct inode* dir, const char* name);
};

struct file_ops;

struct inode {
    inode_type_t type;
    
    struct inode_ops* inode_ops;
    struct file_ops* file_ops;

    void* data;
    size_t size;
    int ref_count;

    struct inode* parent;
};

//fd

typedef struct file {
    struct file_ops* ops;
    struct inode* inode;
    
    void* private_data;
    size_t offset;
    int flags;
    int ref_count;
}file_t;

struct file_ops {
    int (*read)(struct file* file, void* buff, size_t size);
    int (*write)(struct file* file, const void* buf, size_t size);
    int (*open)(struct inode* inode, struct file* file);
    int (*close)(struct file* file);
};

int sys_open(const char* path, size_t len, int flags);
int sys_close(int fd);
int sys_read(int fd, void* buf, size_t size);
int sys_write(int fd, const void* buf, size_t size);
int sys_create(const char* path);

int sys_readdir(int fd, dirent_t* out);
int sys_chdir(const char* path);
int sys_mkdir(const char* path);

struct inode *ramfs_load(void *start, size_t size);
// temporary
void build_path(struct inode* cwd, char* buf);




#endif