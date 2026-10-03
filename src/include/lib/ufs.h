#ifndef FS_H
#define FS_H
#include "types.h"


#define MAX_FD 16

/* Access modes */
#define O_RDONLY    0x0000
#define O_WRONLY    0x0001
#define O_RDWR      0x0002


#define O_ACCMODE   0x0003

/* Creation / behavior flags */
#define O_CREAT     0x0004
#define O_TRUNC     0x0008
#define O_APPEND     0x0010
#define O_BINARY    0x0020



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
    int (*lseek)(struct file* file, long offset, int whence);
};

int fopasgn(file_t* file, struct file_ops* ops);

file_t* file_open(const char* path, int flags);
int sys_open(const char* path, int flags);
int sys_close(int fd);
int sys_read(int fd, void* buf, size_t size);
int sys_write(int fd, const void* buf, size_t size);
int sys_create(const char* path);

int sys_readdir(int fd, dirent_t* out);
int sys_chdir(const char* path);
int sys_getcwd(char* buffer, size_t size);
int sys_mkdir(const char* path);

struct inode *ramfs_load(void *start, size_t size);
// temporary
int build_path(struct inode* cwd, char* buf, size_t size);




#endif