#include "ramfs.h"
#include "memory.h"
#include "string.h"
#include "types.h"
#include "serial.h"
#include "ramfs_image.h"

#define RAMFS_MAX_ENTRIES 32
#define RAMFS_FILE_CAPACITY 4096

static struct inode* ramfs_lookup(struct inode* dir, const char* name);
static int ramfs_create(struct inode* dir, const char* name);

static int ramfs_read(struct file* file, void* buf, size_t size);
static int ramfs_write(struct file* file, const void* buf, size_t size);
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
    .open = NULL,
    .close = NULL
};

typedef struct {
    char name[32];
    struct inode* inode;
} ramfs_entry;

typedef struct {
    ramfs_entry entries[RAMFS_MAX_ENTRIES];
    int count;
} ramfs_dir;

typedef struct {
    uint8_t* data;
    size_t size;
    size_t capacity;
} ramfs_file;


static struct inode* ramfs_alloc_inode(inode_type_t type)
{
    struct inode* inode = kzalloc(sizeof(struct inode));
    
    if (!inode)
        return NULL;

    inode->type = type;
    inode->inode_ops = &ramfs_inode_ops;
    inode->file_ops = &ramfs_file_ops;
    inode->ref_count = 1;

    return inode;
}

struct inode* ramfs_create_root(void* image_start, size_t image_size)
{   

    ramfs_header_t* hdr = (ramfs_header_t*)image_start;

    uint8_t* p = (uint8_t*)image_start;
 

    serial_print("hdr->magic=0x%x, inode_count=%u, root_inode=%u\n",
                hdr->magic, hdr->inode_count, hdr->root_inode);




    struct inode* create_empty_root() {
        struct inode* root = ramfs_alloc_inode(INODE_DIR);
        ramfs_dir* dir = kzalloc(sizeof(ramfs_dir));
        //memset(dir, 0, sizeof(ramfs_dir));
        root->data = dir;
        return root;
    }

    if (!image_start || image_size == 0) {
        // Create empty root
        return create_empty_root();
    }

    // Load from image
    //ramfs_header_t* hdr = (ramfs_header_t*)image_start;
    if (hdr->magic != RAMFS_MAGIC) {
        serial_print("inode count: %d\n", hdr->inode_count);
        serial_print("Invalid RAMFS magic: 0x%x\n", hdr->magic);
        return create_empty_root();
    }

    if (hdr->inode_count == 0) {
        serial_print("RAMFS has no inodes\n");
        return create_empty_root();
    }

    ramfs_disk_inode_t* disk_inodes = (ramfs_disk_inode_t*)(image_start + sizeof(ramfs_header_t));
    uint8_t* data_section = (uint8_t*)(disk_inodes + hdr->inode_count);

    // Allocate array of inodes
    struct inode** inodes = kzalloc(hdr->inode_count * sizeof(struct inode*));
    
    if (!inodes) {
        serial_print("Failed to allocate inode array\n");
        return create_empty_root();
    }

    // Create in-memory inodes
    for (uint32_t i = 0; i < hdr->inode_count; i++) {
        ramfs_disk_inode_t* disk = &disk_inodes[i];
        inode_type_t type = (disk->type == RAMFS_INODE_DIR) ? INODE_DIR : INODE_FILE;
        struct inode* inode = ramfs_alloc_inode(type);
        if (!inode) {
            serial_print("Failed to allocate inode %d\n", i);
            // TODO: cleanup
            return create_empty_root();
        }
        inodes[i] = inode;
    }

    // Set parents
    for (uint32_t i = 0; i < hdr->inode_count; i++) {
        if (disk_inodes[i].parent != UINT32_MAX) {
            inodes[i]->parent = inodes[disk_inodes[i].parent];
        } else {
            inodes[i]->parent = NULL;
        }
    }

    // Set up data for each inode
    for (uint32_t i = 0; i < hdr->inode_count; i++) {
        ramfs_disk_inode_t* disk = &disk_inodes[i];
        if (disk->type == RAMFS_INODE_DIR) {
            ramfs_dir* dir = kzalloc(sizeof(ramfs_dir));
            if (!dir) {
                serial_print("Failed to allocate dir for inode %d\n", i);
                return create_empty_root();
            }
           // memset(dir, 0, sizeof(ramfs_dir));

            // Add children
            uint32_t first = disk->first_child;
            uint32_t count = disk->child_count;
            dir->count = count;

            for (uint32_t j = 0; j < count; j++) {
                uint32_t child_idx = first + j;
                if (child_idx >= hdr->inode_count) {
                    serial_print("Invalid child index %d for inode %d\n", child_idx, i);
                    return create_empty_root();
                }
                strcpy(dir->entries[j].name, disk_inodes[child_idx].name);
                dir->entries[j].inode = inodes[child_idx];
            }

            // Add . and ..
            if (dir->count < RAMFS_MAX_ENTRIES - 2) {
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
            ramfs_file* file = kzalloc(sizeof(ramfs_file));
            if (!file) {
                serial_print("Failed to allocate file for inode %d\n", i);
                return create_empty_root();
            }
            file->data = data_section + disk->data_offset;
            file->size = disk->size;
            file->capacity = disk->size;  // Read-only
            inodes[i]->data = file;
            inodes[i]->size = disk->size;
        }
    }

  //  kfree(inodes);  // WHAT THE FUCK 
    return inodes[hdr->root_inode];
}


// inode ops
struct inode* ramfs_lookup(struct inode* dir, const char* name)
{
    if (dir->type != INODE_DIR)
        return NULL;

    ramfs_dir* d = dir->data;

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
    serial_print("ramfs  create");
    ramfs_dir* d = dir->data;

    if (d->count >= RAMFS_MAX_ENTRIES)
        return -1;

    struct inode* inode = ramfs_alloc_inode(INODE_FILE);

    ramfs_file* f = kzalloc(sizeof(ramfs_file));
    f->data = kzalloc(RAMFS_FILE_CAPACITY);
    f->size = 0;
    f->capacity = RAMFS_FILE_CAPACITY;

    inode->data = f;
    inode->size = 0;

    strcpy(d->entries[d->count].name, name);
    d->entries[d->count].inode = inode;

    d->count++;

    return 0;
}





int ramfs_mkdir(struct inode* dir, const char* name)
{
    ramfs_dir* d = dir->data;


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

    ramfs_dir* new_data = kzalloc(sizeof(ramfs_dir));
  //  memset(new_data, 0, sizeof(ramfs_dir));
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
    ramfs_file* f = file->private_data;

    if (file->offset >= f->size)
        
        return 0;

    size_t remaining = f->size - file->offset;

    if (size > remaining)
        size = remaining;

    memcpy(buf, (char*)f->data + file->offset, size);

    return size;
}
static int ramfs_write(struct file* file, const void* buf, size_t size)
{
    ramfs_file* f = file->private_data;

    if (file->offset + size > f->capacity)
        size = f->capacity - file->offset;

    memcpy(f->data + file->offset, buf, size);

    if (file->offset + size > f->size)
        f->size = file->offset + size;

    if (file->inode)
        file->inode->size = f->size;

    return size;
}

int ramfs_readdir(struct inode* dir, struct dirent* out, int index)
{
    ramfs_dir* d = dir->data;

    if (index >= d->count)
        return -1;

    
    strcpy(out->name, d->entries[index].name);
    out->type = d->entries[index].inode->type;

    return 0;
}

