

#include "types.h"
#include "fs.h"
#include "vfs.h"
#include "memory.h"
#include "task.h"
#include "serial.h"
#include "main.h"
#include "string.h"

#include "ramfs_image.h"





file_t* file_alloc() {
    file_t* f = kmalloc(sizeof(file_t));
    if (!f) return NULL;

    memset(f, 0, sizeof(file_t));
    f->ref_count = 1;
    return f;
}

int fd_alloc(struct process* proc, file_t* file) {
    for (int i = 0; i < MAX_FD; i++) {
        if (proc->fd_table[i] == NULL) {
            proc->fd_table[i] = file;
            return i;
        }
    }
    return -1; // no free fd
}

// actions
int sys_open(const char* path, size_t path_len, int flags) {
    // Create temporary null-terminated path
    char* tmp = kmalloc(path_len + 1);
    if (!tmp) {
        serial_print("no mem for path\n");
        return -1;
    }

    memcpy(tmp, path, path_len);
    tmp[path_len] = '\0';

    // 1. Resolve path → inode
    struct inode* inode;

    if (tmp[0] == '/')
        inode = vfs_lookup(tmp, root_inode);
    else
        inode = vfs_lookup(tmp, current_process->cwd);

    kfree(tmp);

    if (!inode) {
        serial_print("no inode\n");
        return -1; // TODO: -ENOENT
    }

    // 2. Allocate file object
    file_t* file = file_alloc();
    if (!file) {
        serial_print("no mem for file\n");
        return -1; // TODO: -ENOMEM
    }

    // 3. Attach inode + ops
    file->inode = inode;
    file->ops = inode->file_ops;
    file->private_data = inode->data;
    file->offset = 0;
    file->flags = flags;

    // 4. Optional: call open handler
    if (file->ops && file->ops->open) {
        int ret = file->ops->open(inode, file);
        if (ret < 0) {
            kfree(file);
            return ret;
        }
    }

    // 5. Allocate fd
    int fd = fd_alloc(current_process, file);
    if (fd < 0) {
        kfree(file);
        serial_print("no mem for fd\n");
        return -1; // TODO: -EMFILE
    }

    return fd;
}

int sys_close(int fd)
{
    // 1. Validate fd range
    if (fd < 0 || fd >= MAX_FD)
        return -1;

    file_t* file = current_process->fd_table[fd];
    if (!file)
        return -1;   // already closed or never opened

    // 2. Call filesystem-specific close handler
    if (file->ops && file->ops->close)
        file->ops->close(file);

    // 3. Decrement inode reference count
    if (file->inode) {
        file->inode->ref_count--;
        // If ref_count hits 0, you *could* free the inode here,
        // but most simple kernels leave that to the filesystem.
    }

    // 4. Free file structure
    kfree(file);

    // 5. Clear fd table entry
    current_process->fd_table[fd] = NULL;

    return 0;
}


int sys_read(int fd, void* buf, size_t size) {
    // 1. Validate fd
    if (fd < 0 || fd >= MAX_FD) {
        serial_print("read: invalid fd\n");
        return -1; // TODO: -EBADF
        
    }

    file_t* file = current_process->fd_table[fd];
    if (!file) {
        serial_print("read: file not open\n");
        return -1; // TODO: -EBADF
         
    }

    // 2. Check read capability
    if (!file->ops || !file->ops->read) {
        serial_print("read: ni read capability\n");
        return -1; // TODO: -EINVAL or -ENOSYS
         
    }

    // 3. Call underlying implementation
    size_t ret = file->ops->read(file, buf, size);

    // 4. Update offset (only if read succeeded)
    if (ret > 0) {
        file->offset += ret;

        if (file->inode && file->offset > file->inode->size) {
            file->offset = file->inode->size;
        }
    }

    return ret;
}

int sys_write(int fd, const void* buf, size_t size) {
    // 1. Validate fd
    if (fd < 0 || fd >= MAX_FD) {
        return -1; // TODO: -EBADF
    }

    file_t* file = current_process->fd_table[fd];
    if (!file) {
        
        return -1; // TODO: -EBADF
    }

    // 2. Check write capability
    if (!file->ops || !file->ops->write) {
        return -1; // TODO: -EINVAL or -ENOSYS
    }

    // 3. Call underlying implementation
    size_t ret = file->ops->write(file, buf, size);

    // 4. Update offset (only if write succeeded)
    if (ret > 0) {
        file->offset += ret;
    }

    return ret;
}

int sys_create(const char* path) {


    // 1. Resolve parent directory
    struct inode* dir = vfs_lookup_parent(path);
    if (!dir) {
        return -1; // TODO: -ENOENT
    }

    // 2. Extract filename
    const char* name = vfs_path_last(path);
    if (!name) {
        return -1;
    }

    // 3. Check create capability
    if (!dir->inode_ops || !dir->inode_ops->create) {
        return -1; // TODO: -EROFS
    }

    // 4. Call filesystem create
    return dir->inode_ops->create(dir, name);
}

int sys_mkdir(const char* path)
{
    
    char parent_path[256];
    char dir_name[32];

    const char* last_slash = strrchr(path, '/');

    if (!last_slash) {
        // relative path, create in cwd
        strcpy(dir_name, path);
        char buff[32];
        build_path(current_process->cwd, buff);
        strcpy(parent_path, buff);
    } else {
        size_t len = last_slash - path;
        if (len == 0) { // path like "/dirname"
            strcpy(parent_path, "/");
        } else {
            strncpy(parent_path, path, len);
            parent_path[len] = 0;
        }
        strcpy(dir_name, last_slash + 1);
    }

    // 2. Find parent inode
    int fd = sys_open(parent_path,strlen(parent_path), 0);
    if (fd < 0)
        return -1;

    file_t* file = current_process->fd_table[fd];
    struct inode* parent = file->inode;

    if (!parent || parent->type != INODE_DIR)
        return -1;

    int res = parent->inode_ops->mkdir(parent, dir_name);

    return res;
}

int sys_chdir(const char* path) {
    struct inode* newdir;

    if (path[0] == '/')
        newdir = vfs_lookup(path, root_inode);
    else
        newdir = vfs_lookup(path, current_process->cwd);

    if (!newdir || newdir->type != INODE_DIR)
        return -1;

    current_process->cwd = newdir;
    return 0;
}

int sys_readdir(int fd, dirent_t* out) {
    if (fd < 0 || fd >= MAX_FD)
        return -1;

    file_t* file = current_process->fd_table[fd];
    if (!file)
        return -1;

    struct inode* inode = file->inode;

    if (inode->type != INODE_DIR)
        return -1;

    int res = inode->inode_ops->readdir(inode, out, file->offset);
    if (res < 0)
        return res;

    file->offset++;
    return 0;
}


// temporary
const char* find_name_in_parent(struct inode* parent, struct inode* child)
{
    static dirent_t ent;
    int i = 0;

    while (parent->inode_ops->readdir(parent, &ent, i) == 0) {
        struct inode* n = parent->inode_ops->lookup(parent, ent.name);
        if (n == child)
            return ent.name;

        i++;
    }

    return NULL;
}

void build_path(struct inode* cwd, char* buf)
{
    char parts[32][32];
    int depth = 0;

    struct inode* cur = cwd;

    while (cur->parent && cur != cur->parent) {
        const char* name = find_name_in_parent(cur->parent, cur);
        if (!name)
            break;

        strcpy(parts[depth], name);  // Copy the string
        depth++;
        cur = cur->parent;
    }

    int pos = 0;

    if (depth == 0) {
        buf[pos++] = '/';
        buf[pos] = 0;
        return;
    }

    for (int i = depth - 1; i >= 0; i--) {
        buf[pos++] = '/';
        strcpy(&buf[pos], parts[i]);
        pos += strlen(parts[i]);
    }

    buf[pos] = 0;
}