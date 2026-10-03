// kernel/kernel.c
//libs
#include "main.h"
#include "types.h"
#include "stdio.h"
#include "debug.h"
#include "GDT.h"
#include "IDT.h"
#include "io.h"
#include "pmm.h"
#include "vmm.h"
#include "string.h"
#include "ps2_key.h"
#include "sleep.h"
#include "serial.h"
#include "pci.h"
#include "ufs.h"
#include "vfs.h"
#include "ramfs.h"

#include "task.h"
#include "tty.h"
#include "compositor.h"
#include "lpcspeak.h"
#include "ps2_mouse.h"
#include "spinlock.h"
#include "framebuffer.h"
#include "video.h"
#include "psf.h"
#include "bitmap.h"
#include "syscall.h"

#define KERNEL_VER_REL 0
#define KERNEL_VER_MAJ 1
#define KERNEL_VER_MIN 4
#define KERNEL_VER_CODENAME "HEXA"
// drivers
#include "vga-textmode.h"
#include "pit.h"

struct inode* root_inode;

uint32_t MBmagic = 0;
struct multiboot_info* MBinfo;

// temp
void no_sse(){
    return;
}

void kernel_bootstrap(uint32_t magic, struct multiboot_info* mbinfo){
    __asm__ __volatile("cli");

    serial_print("bootstrap procedure\n");
    serial_print("multiboot magic: 0x%x\n", magic);

    if (magic != 0x2BADB002){
        KeBugCheck(0, "MULTIBOOT_MAGIC_INVALID" ,0);
    }

    MBmagic = magic;
    MBinfo = mbinfo;


    const char* cmdline = (const char*) mbinfo->cmdline;
    if (cmdline) {
        serial_print("bootloader commands: '%s'.\n", cmdline);
    }

    // hardware timer frequency
    pit_init(2);

    pmm_init(mbinfo);
    serial_print("pmm initialized\n");
    vmm_init(mbinfo);

    // vmm switches to higher half (kernel_main)

}


void kernel_main() {
    asm __volatile__("cli");

    


    serial_print("executing in higher half\n");


    root_inode = tmpfs_create_empty_root();

    framebuffer_init(MBinfo);

    serial_print("MBinfo at: 0x%x\n", MBinfo);

    multiboot_module_t* mods = (multiboot_module_t*) MBinfo->mods_addr;
    if (MBinfo->mods_count > 0) { // original : 0
        uint32_t mod_start = mods[0].mod_start;
        uint32_t mod_end   = mods[0].mod_end;

        serial_print("=== MODULE DIAGNOSTIC ===\n");
        serial_print("mod_start = 0x%x, mod_end = 0x%x\n", mod_start, mod_end);
        serial_print("Module size: %u bytes\n", mod_end - mod_start);
        
        
        if (strcmp((char*)mods[0].string, "tmpfs") == 0){
            serial_print("module: ramfs_module\n");
            
             
            // Debug the page table entries for specific module pages

            void* fs_start = (void*)mod_start;
            uint32_t fs_size = mod_end - mod_start;

            serial_print("fs_start = 0x%x, fs_size = %d\n", fs_start, fs_size);

            root_inode = set_tmpfs_from_fsimg(fs_start, fs_size);
        } 

    }
    
    // setup vfs
    vfs_init(root_inode);
    init_debug("/sys/fonts/default8x16.psf");

    // syscall handlers
    regDefSyscallHandlers();

    serial_print("initializing scheduler\n");
    init_scheduler();
    

    serial_print("enumerating PCI\n");
    pci_enumerate();
  

    serial_print("=== init tty & input ===\n");
    // input devices and sink's
          tty_init();
        // setup tty
        tty_t* tty0 = create_tty();
        set_active_tty(tty0);
      
        tty_t* active_tty = get_active_tty();

        set_active_sink(&active_tty->input_sink);

    init_serial();
   
    // should be inside driver
    // drivers should register themselves to the input sink
    // kernel should review drivers and register them to the input sink
    ps2keyboard_init();
    mouse_init();

    // scheduler tasks
   

    serial_print("starting critical tasks..\n");
    


    task_t* mcomp_task = create_ktask(compositor_main,NULL);

    process_t* test_proc = create_process_from_elf("program.elf", "/bin/bash.elf", "/");
    
    if(test_proc == NULL){
       KeBugCheck(0,"no proc",0);
    }
    serial_print("test_proc pid: %d\n", test_proc->pid);

    
    
   // task_t* compositor_app = create_ktask((void*)compositor_main, NULL); 
   // active_tty->task_read_wait = console_app->main_task;
   // active_tty->task_backend_wait = compositor_app;
    
    test_proc->main_task->tty = active_tty;
   
   
    serial_print("start scheduling\n");

    start_scheduling();


    while (true)
    {
        KeHalt();
    }
    
}
