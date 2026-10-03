

#include "types.h"
#include "ufs.h"
#include "vfs.h"
#include "vmm.h"
#include "task.h"
#include "serial.h"
#include "main.h"
#include "string.h"


#include "debug.h"

#define KERR_EIO 5
#define KERR_EFAULT 14
#define KERR_ENOENT 2
#define KERR_ENOTDIR 20
#define KERR_EINVAL 22
#define KERR_ERANGE 34


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

// assign file ops to file
int fopasgn(file_t* file, struct file_ops* ops){

    if (file == NULL && ops == NULL){
        return -1;
    }

    file->ops = ops;
    return 0;

}

//  opens file and returns file
file_t* file_open(const char* path, int flags){
    file_t *file;
    char* tmp = 0;
    
    if(path != 0){
        size_t path_len = strlen(path);
    
        // copy from user
        tmp = kmalloc(path_len + 1);
        
        if (!tmp) {
            serial_print("no mem for path\n");
            return NULL;
        }
        memcpy(tmp, path, path_len);
        tmp[path_len] = '\0';
    }else{
        serial_print("[file_open] path is NULL\n");
    }
    
   // serial_print("[file_open] user path: %s\n",tmp); // panics here
    file = vfs_open(tmp, flags);
    
    if(file == NULL){
        // add -ENOENT;
        serial_print("[!!!] sys open: no file for (%s)\n", tmp);
        kfree(tmp);
        return NULL;
    }
    kfree(tmp);

    return file;
    
}

// opens a file and returns file descriptor
int sys_open(const char* path , int flags)
{

    file_t *file;
    int fd;
    char* tmp = 0;
    
    if(path != 0){
        size_t path_len = strlen(path);
    
        // copy from user
        tmp = kmalloc(path_len + 1);
        
        if (!tmp) {
            serial_print("no mem for path\n");
            return -1;
        }
        memcpy(tmp, path, path_len);
        tmp[path_len] = '\0';
    }
    
    serial_print("[sys open] user path: %s\n", tmp);
    if(tmp == 0){
        serial_print("[sys_open] user copied path is null: %s\n", tmp);
        return -1;
    }
    
 
    file = vfs_open(tmp, flags);
    
    if(file == NULL){
        // add -ENOENT;
        serial_print("[!!!] sys open: no file for (%s)\n", tmp);
        kfree(tmp);
        return -1;
    }
    kfree(tmp);

    // 5. Allocate fd
    fd = fd_alloc(current_process, file);
    
    if (fd < 0) {
        kfree(file);
        serial_print("[!!!] sys_open: no space for file for proc: %s\n", current_process->name);
        return -1; // TODO: -EMFILE
    }

    return fd;
}



int sys_read(int fd, void* buf, size_t size) {
    // 1. Validate fd
    if (fd < 0 || fd >= MAX_FD) {
        serial_print("[!!!] sys_read: invalid fd\n");
        return -1; // TODO: -EBADF
        
    }

    file_t* file = current_process->fd_table[fd];
    if (!file) {
        serial_print("[!!!] sys_read: file not open\n");
        return -1; // TODO: -EBADF
         
    }

    // 2. Check read capability
    if (!file->ops || !file->ops->read) {
        serial_print("[!!!] sys_read: no read capability\n");
        return -1; // TODO: -EINVAL or -ENOSYS
         
    }

    // 3. Call underlying implementation
    size_t read_size = vfs_read(file, buf, size);

    // 4. Update offset (only if read succeeded)
    if (read_size > 0) {
        file->offset += read_size;

        if (file->inode && file->offset > file->inode->size) {
            file->offset = file->inode->size;
        }
    }

    return read_size;
}

int sys_write(int fd, const void* buf, size_t size) {
    // 1. Validate fd
    if (fd < 0 || fd >= MAX_FD) {
        serial_print("[!!!] sys_write: invalid fd\n");
        return -1; // TODO: -EBADF
    }

    file_t* file = current_process->fd_table[fd];
    if (!file) {
        serial_print("[!!!] sys_write: file not open\n");
        return -1; // TODO: -EBADF
    }

    if(!file->ops){
        serial_print("[!!!] sys_write: no file ops: 0x%x\n", file->ops);
        return -1;
    }
    if(!file->ops->write){
        serial_print("[!!!] sys_write: no write capability: 0x%x\n", file->ops->write);
        return -1; // TODO: -EINVAL or -ENOSYS
    }

    // 3. Call underlying implementation
    size_t write_size = vfs_write(file, buf, size);

    // 4. Update offset (only if write succeeded)
    if (write_size > 0) {
        file->offset += write_size;
    }

    return write_size;
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
        serial_print("no name\n");
        return -1;
    }

    // 3. Check create capability
    if (!dir->inode_ops || !dir->inode_ops->create) {
        serial_print("cant create\n");
        return -1; // TODO: -EROFS
    }

    // 4. Call filesystem create
    return dir->inode_ops->create(dir, name);
}

int sys_close(int fd)
{
    if (fd < 0 || fd >= MAX_FD || !current_process) {
        return -1;
    }

    struct file *file = current_process->fd_table[fd];
    if (!file) {
        return -1; // implement EBADF
    }

    current_process->fd_table[fd] = NULL;
    vfs_close(file);
    return 0;
}

int sys_mkdir(const char* path)
{
    
    char parent_path[256];
    char dir_name[32];

    const char* last_slash = strrchr(path, '/');

    if (!last_slash) {
        // relative path, create in cwd
        strcpy(dir_name, path);
        char buff[256];
        if (build_path(current_process->cwd, buff, sizeof(buff)) < 0)
            return -1;
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
    int fd = sys_open(parent_path, 0);
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
    if (!path)
        return -KERR_EFAULT;
    if (!current_process || !current_process->cwd)
        return -KERR_EIO;

    struct inode* newdir;

    if (path[0] == '/')
        newdir = vfs_lookup(path, root_inode);
    else
        newdir = vfs_lookup(path, current_process->cwd);

    if (!newdir)
        return -KERR_ENOENT;
    if (newdir->type != INODE_DIR)
        return -KERR_ENOTDIR;

    inode_ref(newdir);
    inode_unref(current_process->cwd);
    current_process->cwd = newdir;
    return 0;
}

int sys_getcwd(char* buffer, size_t size)
{
    if (!buffer)
        return -KERR_EFAULT;
    if (size == 0)
        return -KERR_EINVAL;
    if (!current_process || !current_process->cwd)
        return -KERR_EIO;

    return build_path(current_process->cwd, buffer, size);
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

    if (!parent || !parent->inode_ops || !parent->inode_ops->readdir ||
        !parent->inode_ops->lookup)
        return NULL;

    while (parent->inode_ops->readdir(parent, &ent, i) == 0) {
        struct inode* n = parent->inode_ops->lookup(parent, ent.name);
        if (n == child)
            return ent.name;

        i++;
    }

    return NULL;
}

int build_path(struct inode* cwd, char* buf, size_t size)
{
    char parts[32][32];
    int depth = 0;
    size_t path_len = 0;

    if (!cwd || !buf)
        return -KERR_EFAULT;

    struct inode* cur = cwd;

    while (cur->parent && cur != cur->parent) {
        if (depth >= (int)(sizeof(parts) / sizeof(parts[0])))
            return -KERR_ERANGE;

        const char* name = find_name_in_parent(cur->parent, cur);
        if (!name)
            return -KERR_EIO;

        size_t name_len = 0;
        while (name[name_len])
            name_len++;
        if (name_len >= sizeof(parts[depth]))
            return -KERR_EIO;
        memcpy(parts[depth], name, name_len + 1);
        path_len += name_len + 1;
        depth++;
        cur = cur->parent;
    }

    if (depth == 0)
        path_len = 1;
    if (size <= path_len)
        return -KERR_ERANGE;

    int pos = 0;

    if (depth == 0) {
        buf[pos++] = '/';
        buf[pos] = 0;
        return 0;
    }

    for (int i = depth - 1; i >= 0; i--) {
        buf[pos++] = '/';
        strcpy(&buf[pos], parts[i]);
        pos += strlen(parts[i]);
    }

    buf[pos] = 0;
    return 0;
}