#ifndef VFS_H
#define VFS_H
#include "types.h"
#include "fs.h"

void vfs_init(struct inode* root);
struct inode* vfs_lookup(const char* path, struct inode* base);
struct inode* vfs_lookup_parent(const char* path);
struct inode* inode_ref(struct inode* inode);
void inode_unref(struct inode* inode);
const char* vfs_path_last(const char* path);

#endif