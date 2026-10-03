#include "ramfs.h"
#include "ufs.h"
#include "vfs.h"
#include "vmm.h"
#include "string.h"
#include "types.h"
#include "serial.h"
#include "fsimg.h"
#include "debug.h"


static struct inode* ramfs_lookup(struct inode* dir, const char* name);
static int ramfs_create(struct inode* dir, const char* name);

static int ramfs_read(struct file* file, void* buf, size_t size);
static int ramfs_write(struct file* file, const void* buf, size_t size);
static int ramfs_lseek(struct file* file, long offset, int whence);
int ramfs_readdir(struct inode* dir, struct dirent* out, int index);
int ramfs_mkdir(struct inode* dir, const char* name);

static struct inode_ops ramfs_inode_ops = {
    .lookup = ramfs_lookup,
    .create = ramfs_create,
    .mkdir = ramfs_mkdir,
    .unlink = NULL,
    .readdir = ramfs_readdir
};


static struct file_ops ramfs_file_ops = {
    .read = ramfs_read,
    .write = ramfs_write,
    .lseek = ramfs_lseek,
    .open = NULL,
    .close = NULL
};



static struct inode* alloc_inode(inode_type_t type)
{
    struct inode* inode = vfs_creat_inode( type);

    inode->inode_ops = &ramfs_inode_ops;
    inode->file_ops = &ramfs_file_ops;
    inode->ref_count = 1;

    return inode;
}


struct inode* tmpfs_create_empty_root() {
    struct inode* root = alloc_inode(INODE_DIR);
    fsimg_dir* dir = kzalloc(sizeof(fsimg_dir));
    root->data = dir;
    return root;
}


struct inode* set_tmpfs_from_fsimg(void* image_start, size_t image_size)
{   

    fsimg_header_t* hdr = (fsimg_header_t*)image_start;

    uint8_t* p = (uint8_t*)image_start;
 

    if (!image_start || image_size == 0) {
        // Create empty root
        serial_print("[!!!] empty root: ramfs image null start(0x%x) size(%d)\n",p, image_size);
        return tmpfs_create_empty_root();
    }

    // Load from image
    if (hdr->magic != FSIMG_MAGIC) {
        serial_print("inode count: %d\n", hdr->inode_count);
        serial_print("Invalid RAMFS magic: 0x%x\n", hdr->magic);
        return tmpfs_create_empty_root();
    }

    if (hdr->inode_count == 0) {
        serial_print("RAMFS has no inodes\n");
        return tmpfs_create_empty_root();
    }


    fsimg_inode_t* fsimg_inodes = (fsimg_inode_t*)(image_start + sizeof(fsimg_header_t));
    uint8_t* data_section = (uint8_t*)(fsimg_inodes + hdr->inode_count);

    // Allocate array of inodes
    struct inode** inodes = kzalloc(hdr->inode_count * sizeof(struct inode*));
    
    if (!inodes) {
        serial_print("Failed to allocate inode array\n");
        return tmpfs_create_empty_root();
    }

    // Create in-memory inodes
    for (uint32_t i = 0; i < hdr->inode_count; i++) {
        fsimg_inode_t* fsimg_ino = &fsimg_inodes[i];
        inode_type_t type = (fsimg_ino->type == FSIMG_INODE_DIR) ? INODE_DIR : INODE_FILE;
        struct inode* inode = alloc_inode(type);
        if (!inode) {
            serial_print("Failed to allocate inode %d\n", i);
            // TODO: cleanup
            return tmpfs_create_empty_root();
        }

        inodes[i] = inode;
    }

    

    // Set parents
    for (uint32_t i = 0; i < hdr->inode_count; i++) {
        if (fsimg_inodes[i].parent != UINT32_MAX) {
            inodes[i]->parent = inodes[fsimg_inodes[i].parent];
        } else {
            inodes[i]->parent = NULL;
        }
    }

    // Set up data for each inode
    for (uint32_t i = 0; i < hdr->inode_count; i++) {
        fsimg_inode_t* fsimg_ino = &fsimg_inodes[i];
            if (fsimg_ino->type == FSIMG_INODE_DIR) {
            fsimg_dir* dir = kzalloc(sizeof(fsimg_dir));
            if (!dir) {
                serial_print("Failed to allocate dir for inode %d\n", i);
                return tmpfs_create_empty_root();
            }

            // Add children
            uint32_t first = fsimg_ino->first_child;
            uint32_t count = fsimg_ino->child_count;
            dir->count = count;

            for (uint32_t j = 0; j < count; j++) {
                uint32_t child_idx = first + j;
                if (child_idx >= hdr->inode_count) {
                    serial_print("Invalid child index %d for inode %d\n", child_idx, i);
                    return tmpfs_create_empty_root();
                }
                strcpy(dir->entries[j].name, fsimg_inodes[child_idx].name);
                dir->entries[j].inode = inodes[child_idx];
            }

            // Add . and ..
            if (dir->count < FSIMG_MAX_ENTRIES - 2) {
                strcpy(dir->entries[dir->count].name, ".");
                dir->entries[dir->count].inode = inodes[i];
                dir->count++;

                strcpy(dir->entries[dir->count].name, "..");
                dir->entries[dir->count].inode = inodes[i]->parent ? inodes[i]->parent : inodes[i];
                dir->count++;
            }

            inodes[i]->data = dir;
        } else {
            // File
            fsimg_file* file = kzalloc(sizeof(fsimg_file));
            if (!file) {
                serial_print("Failed to allocate file for inode %d\n", i);
                return tmpfs_create_empty_root();
            }
            file->data = data_section + fsimg_ino->data_offset;
            file->capacity = fsimg_ino->size;
            file->size = fsimg_ino->size;
            inodes[i]->data = file;
            inodes[i]->size = fsimg_ino->size;
           
        }
        
    }

    serial_print("successfully setup fs. inodes(%d)\n", hdr->inode_count);
    return inodes[hdr->root_inode];
}


// inode ops
struct inode* ramfs_lookup(struct inode* dir, const char* name)
{
    if (dir->type != INODE_DIR)
        return NULL;

    fsimg_dir* d = dir->data;

    if(strcmp(name, ".") == 0){
        return dir;
    }

    if(strcmp(name, "..") == 0){
        return dir->parent ? dir->parent : dir;
    }

    for (int i = 0; i < d->count; i++) {
        if (strcmp(d->entries[i].name, name) == 0) {
            return d->entries[i].inode;   // IMPORTANT
            
        }
    }
    
    

    return NULL;
}

static int ramfs_create(struct inode* dir, const char* name)
{
    fsimg_dir* d = dir->data;

    if (d->count >= FSIMG_MAX_ENTRIES)
        return -1;

    struct inode* inode = alloc_inode(INODE_FILE);

    fsimg_file* f = kzalloc(sizeof(fsimg_file));
    f->data = kzalloc(FSIMG_FILE_CAPACITY);
    f->size = 0;
    f->capacity = FSIMG_FILE_CAPACITY;

    inode->data = f;
    inode->size = 0;

    strcpy(d->entries[d->count].name, name);
    d->entries[d->count].inode = inode;

    d->count++;

    return 0;
}





int ramfs_mkdir(struct inode* dir, const char* name)
{
    fsimg_dir* d = dir->data;


    if (d->count >= 64)
        return -1;

    for (int i = 0; i < d->count; i++) {
        if (strcmp(d->entries[i].name, name) == 0)
            return -1;
    }

    struct inode* new_inode = kzalloc(sizeof(struct inode));
   // memset(new_inode, 0, sizeof(struct inode));

    new_inode->type = INODE_DIR;
    new_inode->inode_ops = &ramfs_inode_ops;
    new_inode->parent = dir;

    fsimg_dir* new_data = kzalloc(sizeof(fsimg_dir));
  //  memset(new_data, 0, sizeof(fsimg_dir));
    new_inode->data = new_data;


    new_data->entries[0].inode = new_inode; // self
    strcpy(new_data->entries[0].name, ".");
    new_data->count++;

    
    new_data->entries[1].inode = dir;  // parent
    strcpy(new_data->entries[1].name, "..");
    new_data->count++;
    
    d->entries[d->count].inode = new_inode;
    strcpy(d->entries[d->count].name, name);
    d->count++;

    return 0;
}

static int ramfs_read(struct file* file, void* buf, size_t size)
{

    if(!buf){
        serial_print("[!!!] ramfs_read: invalid buffer\n");
        return 0;
    }
    
    serial_print("ramfs reading\n");
    fsimg_file* f = file->private_data;

    if (file->offset >= f->size) {
        serial_print("[!!!] ramfs_read: file offset greater than size (o: %d s: %d)\n",file->offset, f->size);
        return 0;
    }

    serial_print("size:%d\n",f->size);
    serial_print("data at 0x%x\n", f->data);
    serial_print("buffer at 0x%x\n",  buf);
    serial_print("offset: %d\n",file->offset);

    size_t remaining = f->size - file->offset;
    
    if (size > remaining)
        size = remaining;


    memcpy(buf, (char*)f->data + file->offset, size);

    return (int)size;
}
static int ramfs_write(struct file* file, const void* buf, size_t size)
{
    fsimg_file* f = file->private_data;

    if (file->offset + size > f->capacity)
        size = f->capacity - file->offset;

    memcpy(f->data + file->offset, buf, size);

    if (file->offset + size > f->size)
        f->size = file->offset + size;

    if (file->inode)
        file->inode->size = f->size;

    return size;
}

static int ramfs_lseek(struct file* file, long offset, int whence){
    
    #define SEEK_SET 0
    #define SEEK_CUR 1
    #define SEEK_END 2

    #define LSEEK_OK        0
    #define LSEEK_EINVAL   -1
    #define LSEEK_EBADF    -2
    #define LSEEK_EOVERFLOW -3
    
    long new_offset;

    if (file == (file_t *)0)
        return LSEEK_EBADF;

    switch (whence) {
        case SEEK_SET:
            new_offset = offset;
            break;

        case SEEK_CUR:
            new_offset = (long)file->offset + offset;
            break;

        case SEEK_END:
            if (file->inode == (struct inode *)0)
                return LSEEK_EINVAL;

            new_offset = (long)file->inode->size + offset;
            break;

        default:
            return LSEEK_EINVAL;
    }

    if (new_offset < 0)
        return LSEEK_EINVAL;

    file->offset = (size_t)new_offset;

    return (size_t)new_offset;
}

int ramfs_readdir(struct inode* dir, struct dirent* out, int index)
{
    fsimg_dir* d = dir->data;

    if (index >= d->count)
        return -1;

    
    strcpy(out->name, d->entries[index].name);
    out->type = d->entries[index].inode->type;

    return 0;
}

