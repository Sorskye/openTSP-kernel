#include "ufs.h"
#include "types.h"
#include "vfs.h"
#include "string.h"
#include "serial.h"
#include "vmm.h"



static struct inode* root_inode = NULL;

void vfs_init(struct inode* root) {
    root_inode = root;
}


// aka iget
struct inode* inode_ref(struct inode* inode) {
    if (inode)
        inode->ref_count++;
    return inode;
}

// aka iput
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

    const char* last = vfs_path_last(path);
    if (!last || last <= path)
        return NULL;

    size_t plen = (size_t)(last - 1 - path); /* chars before the final '/' */

    char temp[256];
    if (plen >= sizeof(temp))
        return NULL; /* or handle dynamically */

    if (plen == 0) {
        temp[0] = '/';
        temp[1] = '\0';
    } else {
        memcpy(temp, path, plen);
        temp[plen] = '\0';
    }

    return vfs_lookup(temp, NULL);
}

struct inode* vfs_creat_inode(inode_type_t type){
    struct inode* inode = kzalloc(sizeof(struct inode));

    if (!inode)
        return NULL;

    inode->type = type;

    return inode;
}


struct file *vfs_open(const char *path, int flags){
    
    struct inode* inode;
    struct file* file;

    inode = vfs_lookup(path, root_inode);

    if (!inode && flags & O_CREAT) {
        inode = vfs_creat_inode(INODE_FILE);
        serial_print("[vfs_open] file nonexistent, creating..\n");
    }
    
    
    if(!inode){
        serial_print("[vfs_open]: no inode\n");
        return NULL;
    }
    
    file = kmalloc(sizeof(struct file));
    if(file == NULL){
        serial_print("[!!!] vfs_open: no file allocated\n");
        inode_unref(inode);
        return NULL;
    }


    file->inode = inode;
    
    file->ops = inode->file_ops;

    file->private_data = inode->data;
    file->offset = 0;
    file->flags = flags & O_ACCMODE;

    // TEMP:
    // call open handler
    return file;

}

size_t vfs_read(struct file *file, void *buf, size_t size){
    if (size == 0)
        return 0;

    if (!file || !file->inode || !file->inode->file_ops ||
        !file->inode->file_ops->read || !buf ||
        (file->flags & O_ACCMODE) == O_WRONLY) {
        serial_print("vfs read: invalid\n");
        serial_print("vfs read: buf: 0x%x\n", buf);
        serial_print("vfs read: size: %d\n", size);
        return (size_t)-1;
    }

    return file->inode->file_ops->read(file, buf, size);
}

size_t vfs_lseek(struct file* file, long offset, int whence){
    return file->inode->file_ops->lseek(file, offset, whence);
}


size_t vfs_write(struct file *file, const void *buf, size_t size){
    //todo: check if file supports writing
    return file->inode->file_ops->write(
        file,
        buf,
        size
    );
}

void vfs_close(struct file* file){
    if(file == NULL){
        return;// implement EINVAL
    }

    if(file->ops && file->ops->close){
        file->ops->close(file);
    }

    // todo file_put
    return;
}