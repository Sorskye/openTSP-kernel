#include "fs.h"
#include "types.h"
#include "vfs.h"
#include "string.h"

static struct inode* root_inode = NULL;

void vfs_init(struct inode* root) {
    root_inode = root;
}

struct inode* inode_ref(struct inode* inode) {
    if (inode)
        inode->ref_count++;
    return inode;
}

void inode_unref(struct inode* inode) {
    if (!inode)
        return;

    inode->ref_count--;

    if (inode->ref_count == 0) {
        // TODO: filesystem specific cleanup
    }
}

struct inode* vfs_lookup(const char* path, struct inode* base){

    if (!root_inode || !path)
        return NULL;


    /* absolute paths only */
    if (*path == '/')
    {
        base = root_inode;
        path++;
    }
        

    char name[256];

    while (*path)
    {
        int len = 0;

        /* extract next path component */
        while (*path && *path != '/') {
            if (len >= sizeof(name) - 1)
                return NULL;
            name[len++] = *path++;
        }

        name[len] = '\0';

        if (*path == '/')
            path++;

        if (!base->inode_ops || !base->inode_ops->lookup)
            return NULL;

        struct inode* next = base->inode_ops->lookup(base, name);
        if (!next)
            return NULL;
        inode_ref(next);

        base = next;
    }
    return base;
}

const char* vfs_path_last(const char* path)
{
    const char* last = path;

    while (*path) {
        if (*path == '/')
            last = path + 1;
        path++;
    }

    return last;
}

struct inode* vfs_lookup_parent(const char* path)
{
    if (!path || path[0] != '/')
        return NULL;

    char temp[256];
    int len = 0;

    const char* last = vfs_path_last(path);

    const char* p = path;

    /* build parent path */
    while (*p && p != last - 1) {
        temp[len++] = *p++;
    }

    if (len == 0) {
        temp[len++] = '/';
    }

    temp[len] = '\0';

    return vfs_lookup(temp,NULL);
}