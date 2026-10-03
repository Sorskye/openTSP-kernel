#ifndef VFS_H
#define VFS_H
#include "types.h"
#include "ufs.h"



void vfs_init(struct inode* root);
struct inode* vfs_lookup(const char* path, struct inode* base);
struct inode* vfs_lookup_parent(const char* path);
struct inode* inode_ref(struct inode* inode);
void inode_unref(struct inode* inode);
const char* vfs_path_last(const char* path);


struct file* vfs_open(const char *path, int flags);
size_t vfs_read(struct file *file, void *buf, size_t size);
size_t vfs_write(struct file *file, const void *buf, size_t size);
size_t vfs_lseek(struct file* file, long offset, int whence);
struct inode* vfs_creat_inode(inode_type_t type);
void vfs_close(struct file* file);
#endif