
#include "types.h"
#include "task.h"
#include "vga-textmode.h"
#include "string.h"
#include "GDT.h"
#include "serial.h"
#include "vmm.h"
#include "sleep.h"
#include "tty.h"
#include "main.h"
#include "vfs.h"
#include "lpcspeak.h"
#include "debug.h"
#include "TSS.h"
#include "elf_loader.h"

// extern assembly routine
extern void user_task_enter();
extern void user_task_return();

extern void context_switch(uint32_t **old_sp_ptr, uint32_t *new_sp);
extern void task_trampoline();
extern void user_task_trampoline();
static bool CanSchedule = false;

static task_t task_table[MAX_TASKS];
task_t* task_table_head = NULL;

process_t* kernel_process = NULL;
process_t* current_process = NULL;
task_t *current_task = NULL;
task_t *idle_task = NULL;
task_t *task_head = 0;

uint32_t task_count = 0;
static uint32_t tid_count = 0;

uint32_t process_count = 0;
static uint32_t pid_count = 0;

#define INITIAL_EFLAGS 0x202
#define USER_INITIAL_EFLAGS (INITIAL_EFLAGS | (3 << 12))

volatile uint32_t system_ticks = 0;
volatile uint32_t usage_ticks = 0;
volatile uint32_t busy_ticks = 0;
volatile uint8_t usage = 0;




void block_task(task_t *task){
    if (!task) return;
    task->state = TASK_BLOCKED;
    return;
}

void wake_task(task_t *task){
    if (!task) return;
    task->state = TASK_READY;
    return;
}





void remove_task_from_list(task_t* task){
    task_t* prev_task = &task_table[task->tid - 1];
    prev_task->next = &task_table[task->next->tid];
    return;
}

uint32_t get_task_table(task_t* tasktable){
    memcpy(tasktable, task_table, task_count);
    return task_count;
}

uint32_t get_task_count(){
    return task_count;
}

// runs in interrupt context !
void scheduler_update_time(void) {

    system_ticks++;
    usage_ticks++;

    if(current_task != idle_task){
        busy_ticks++;
    }

    size_t cnt = 0;
    for (task_t *t = task_head; t; t = t->next) {
        if (t->state == TASK_SLEEPING && (int32_t)(t->wake_tick - system_ticks) <= 0) {
            t->state = TASK_READY;
        }
        if (cnt++ >= task_count) break;
    }
    return;
}



//!! add page directories
task_t* scheduler_choose_next(void) {
    
    if (!current_task || !task_head)
        return idle_task;

    task_t *next = current_task;
    size_t max = task_count;
    
    while (max--) {
        next = next->next ? next->next : task_head;
    
        if(!next){
            serial_print("no next task\n");
            KeBugCheck(0, "SCHEDULER_HEAD_TAIL_ERROR", 0);
        }

        if(!next->parent_process){
            KeBugCheck(0,"SCHEDULER_RETURNED_PARENTLESS_TASK",0);
        }


        if (next->state == TASK_READY || next->tid == idle_task->tid) {
            return next; 
        }
    
        if (next->state == TASK_EXITED) {
            remove_task_from_list(next);
        }
    }


    return idle_task;

}

void scheduler_tick(void) {
    if (!CanSchedule) return;

    scheduler_update_time();

    task_t *old = current_task;
    task_t *next = scheduler_choose_next();
    
    if (!next) next = idle_task;
    
    if(next == idle_task){
    }

    if (next != old) {
        current_task = next;

        if(current_task->esp == 0){
            KeBugCheck(0,"TASK_WITHOUT_SP_SCHEDULED", 0);
        }

        current_process = next->parent_process;

        // update TSS ring0 stack for next task
        tss_set_kernel_stack(next->kstack_top);

        // switch address space if you have per-process paging
        if (current_process != NULL && next->parent_process && (!old || old->parent_process != next->parent_process)) {
            if (next->parent_process->vm_env) {
                switch_page_directory(next->parent_process->vm_env->pd_addr);
            }else{
                // use master pd if no is set ()
            }
            
        }
        
    }

    if (next == idle_task) {
        asm("nop");
    }  
     
    return;
}


// ONLY RUN WITH INTERRUPTS DISABLED!
task_t* alloc_task() {
    if (task_count >= MAX_TASKS){ return NULL; }

    task_t *t = kzalloc(sizeof(task_t));
    if (t == NULL) return NULL;

    memset(t, 0xCC, sizeof(task_t));

    serial_print("setting tid to :%d\n",tid_count);

    t->tid = tid_count;

    task_count++;
    tid_count++;

    t->next = NULL;
    t->state = TASK_BLOCKED;
    t->kstack_top = (uint32_t)(t->stack + KERNEL_STACK_SIZE);


    if (!task_head) {
        task_head = t;
        t->next = t;
    } else {
        task_t *tail = task_head;
        int i = 0;
        while (tail->next != task_head) {
            tail = tail->next;
            i++;
        }
        tail->next = t;
        t->next = task_head;

        if (i > MAX_TASKS) {
            KeBugCheck(3,0,0); // task queue violation
        }
    }

  

    return t;
}


void trigger_schedule(){
    asm volatile ("int $0x20");
    // no ret
}

void task_exit(task_t *task){
    serial_print("task exit\n");
    task->state = TASK_EXITED;
}

void process_exit(process_t* process){
    serial_print("process exit\n");
    for(int i = 0; i < MAX_PROCESS_TASKS; i++){
        task_t* task = process->task_table[i];
        if(task && task->esp != NULL){
            task_exit(task);
        }
    }
}



// TODO include args in task_fn
task_t* create_ktask(task_fn fn, void *arg) {
    
    task_t* t = alloc_task();
    if (!t) {
        serial_print("ktask alloc failed\n");
        return NULL;
    }
    if(!kernel_process){
        serial_print("no kernel process for task\n");
        return NULL;
    }

    uint32_t *sp = (uint32_t*)(t->stack + KERNEL_STACK_SIZE);
    t->kstack_top = (uint32_t)(t->stack + KERNEL_STACK_SIZE);

    t->mode = TASK_KERNEL;
    t->user_stack_top = 0;
    t->parent_process = kernel_process;

    // iret frame
    *(--sp) = INITIAL_EFLAGS;
    *(--sp) = KERNEL_CODE_SELECTOR;
    *(--sp) = (uint32_t)task_trampoline;

    // pusha frame
    *(--sp) = 0x0;            // eax
    *(--sp) = 0x0;            // ecx
    *(--sp) = 0x0;            // edx
    *(--sp) = (uint32_t)fn;            // ebx
    *(--sp) = 0x0;            // esp dummy
    *(--sp) = 0x0;            // ebp
    *(--sp) = (uint32_t)arg;  // esi
    *(--sp) = 0x0;            // edi

    t->esp = sp;
    t->state = TASK_READY;
    serial_print("created task. state at 0x%x\n", t->state);
    return t;
}

task_t* create_ptask(process_t* proc, void* entry) {
    if (!proc) return NULL;

    asm volatile("cli");

    // Find a free slot in the parent's task table first
    int slot = -1;
    for (int i = 0; i < MAX_PROCESS_TASKS; i++) {
        if (proc->task_table[i] == NULL) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        return NULL; // process already has the max number of tasks
    }

    task_t* t = alloc_task();
    if (!t) {
        return NULL;
    }

    uint32_t *sp = (uint32_t*)(t->stack + KERNEL_STACK_SIZE);

    t->kstack_top = (uint32_t)(t->stack + KERNEL_STACK_SIZE);
    t->user_stack_top = USER_STACK_TOP;
    t->mode = TASK_USER;
    t->parent_process = proc;

    // Register the task with its parent process
    proc->task_table[slot] = t;
    if (!proc->main_task) {
        proc->main_task = t; // first task becomes the main task
    }

    // iret frame for ring 3
    *(--sp) = USER_DATA_SELECTOR;
    *(--sp) = USER_STACK_TOP - sizeof(uint32_t);
    *(--sp) = USER_INITIAL_EFLAGS;
    *(--sp) = USER_CODE_SELECTOR;
    *(--sp) = (uint32_t)user_task_trampoline;

    // kernel-mode frame for user_task_enter
    *(--sp) = INITIAL_EFLAGS;
    *(--sp) = KERNEL_CODE_SELECTOR;
    *(--sp) = (uint32_t)user_task_enter;

    // pusha frame
    *(--sp) = 0x0;            // eax
    *(--sp) = 0x0;            // ecx
    *(--sp) = 0x0;            // edx
    *(--sp) = (uint32_t)entry; // ebx: trampoline reads entry from here
    *(--sp) = 0x0;            // esp dummy
    *(--sp) = 0x0;            // ebp
    *(--sp) = 0x0;            // esi
    *(--sp) = 0x0;            // edi

    t->esp = sp;
    t->state = TASK_READY;

    
    return t;
}

process_t* create_process_from_elf(char* name, char* path, char* workingDirectory){
    process_t* proc = kzalloc(sizeof(process_t));
    if(proc==NULL){return 0; }

    struct inode* cwd = vfs_lookup(workingDirectory, root_inode);
    if (cwd == NULL) {return 0; }

    memset(proc, 0, sizeof(process_t));

    // clear fd table
    for(int i = 0; i < MAX_FD; i++){
        proc->fd_table[i] = NULL;
    }

    const char* stdtty = "/dev/tty0";
    serial_print("stdtty %s\n",stdtty);
    // stdin, stdout, stderr
    proc->fd_table[0] = file_open(stdtty, O_RDONLY | O_CREAT);
    proc->fd_table[1] = file_open(stdtty, O_WRONLY | O_CREAT);
    proc->fd_table[2] = file_open(stdtty, O_WRONLY | O_CREAT);

    serial_print("stdin file ops at 0x%x\n", proc->fd_table[0]->inode->file_ops); 

    proc->cwd = cwd;
    inode_ref(cwd);

    proc->vm_env = allocUserVmEnv();

    elf_load_result_t res;
    if(elf_load(path, proc->vm_env, &res) != 0 ){
        // process_destroy(proc)
        return NULL;
    }

    proc->vm_env->vheap.start = res.heap_start;
    proc->vm_env->vheap.end = res.heap_start;
    proc->vm_env->vheap.current = res.heap_start;
    proc->vm_env->vheap.max = USER_STACK_BASE - (8U * PAGE_SIZE);

    task_t* main_task = create_ptask(proc, (void*)res.entry);
    if(main_task == NULL){return NULL;}

    main_task->parent_process = proc;
    proc->main_task = main_task;
    proc->pid = pid_count;
  
  
    strncpy(proc->name, name, MAX_PROCESS_NAME_LEN - 1);
    proc->name[MAX_PROCESS_NAME_LEN - 1] = '\0';
    
    // race condition?
    process_count++;
    pid_count++;

    return proc;
}

process_t* create_empty_kernel_process(char* name){

    process_t* proc = kzalloc(sizeof(process_t));
    if(proc == NULL){ return 0; }

    memset(proc, 0, sizeof(process_t));

    for(int i = 0; i < MAX_FD; i++)
    proc->fd_table[i] = NULL;

    proc->cwd = root_inode;
    inode_ref(root_inode);

    proc->pid = pid_count;
    
    proc->vm_env = getSupervisorVmEnv();
    
  
    strncpy(proc->name, name, MAX_PROCESS_NAME_LEN - 1);
    proc->name[MAX_PROCESS_NAME_LEN - 1] = '\0';
    
    process_count++;
    pid_count++;

    return proc;
}

process_t* create_process(char* name, task_fn fn){
    
    // !! update to kernel heap (kzalloc)
    process_t* proc = kzalloc(sizeof(process_t));
    if(proc==NULL){return 0;}
  
    memset(proc, 0, sizeof(process_t));

    for(int i = 0; i < MAX_FD; i++)
    proc->fd_table[i] = NULL;

    proc->cwd = root_inode;
    inode_ref(root_inode);

    proc->vm_env = allocUserVmEnv();
    // 
    // load_elf(proc-page_directory, elf_data)
    // entry = elf_get_entry(elf_data);
    
    
    task_t* main_task = create_ptask(proc, (void*)fn);
    if(main_task == NULL){asm("sti");return 0;}

    main_task->parent_process = proc;
    proc->main_task = main_task;
    proc->pid = pid_count;
  
    strncpy(proc->name, name, MAX_PROCESS_NAME_LEN - 1);
    proc->name[MAX_PROCESS_NAME_LEN - 1] = '\0';
    
    process_count++;
    pid_count++;

    return proc;
}


void idle_func(){
    while (1){
        serial_print("idle\n");
        __asm__ __volatile__("nop");
    }
}

task_t* get_idle_task(){
    return idle_task;
}

void init_scheduler(){

    kernel_process = create_empty_kernel_process("kernel.sys");
    if(kernel_process == 0){
        KeBugCheck(0, "KERNEL_PROCESS_FAILED_TO_START\n", NULL);
    }
    serial_print("kproc pid: %d\n",kernel_process->pid);

    task_t* new_idle_task = create_ktask((void*)idle_func, 0);

    idle_task = new_idle_task;

    return;
}


void start_scheduling(void) {
    if (!task_head) return;
    CanSchedule = true;
    current_task = task_head;
    uint32_t *kernel_esp;
    serial_print("context switching..\n");
    asm volatile("sti");
    for(;;){}
}

void stop_scheduling(void){
    asm volatile("cli");
    CanSchedule = false;
    asm volatile("sti");
}

