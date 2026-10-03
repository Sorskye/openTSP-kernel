
#include "types.h"
#include "task.h"
#include "vga-textmode.h"
#include "string.h"
#include "GDT.h"
#include "serial.h"
#include "memory.h"
#include "sleep.h"
#include "tty.h"
#include "main.h"
#include "vfs.h"
#include "lpcspeak.h"
#include "kerror.h"

// extern assembly routine
extern void context_switch(uint32_t **old_sp_ptr, uint32_t *new_sp);
static bool CanSchedule = false;

static task_t task_table[MAX_TASKS];
task_t* task_table_head = NULL;

process_t* current_process = NULL;
task_t *current_task = NULL;
task_t *idle_task = NULL;
task_t *task_head = 0;

uint32_t task_count = 0;
static uint32_t tid_count = 0;

uint32_t process_count = 0;
static uint32_t pid_count = 0;

#define INITIAL_EFLAGS 0x202  

volatile uint32_t system_ticks = 0;
volatile uint32_t usage_ticks = 0;
volatile uint32_t busy_ticks = 0;
volatile uint8_t usage = 0;


void idle_func(){
    while (1){
        __asm__ __volatile__("hlt");
    }
}



void block_task(task_t *task){
    task->state = TASK_BLOCKED;
    return;
}

void wake_task(task_t *task){
    task->state = TASK_READY;
    return;
}

void terminate_task(task_t *task){
    task->state = TASK_EXITED;
    return;
}



void remove_task_from_list(task_t* task){
    if (task->state != TASK_EXITED){
        task->state = TASK_EXITED;
    }

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
        
        if (next->state == TASK_READY || next->tid == idle_task->tid) {
         //   serial_print("Switching to task: %d\n", next->tid);
            return next;
            
        }

        if (next->state == TASK_EXITED) {
            remove_task_from_list(next);
        }
    }

    serial_print("IDLE\n");
    return idle_task;

}

void scheduler_tick(void) {
   
     if(!CanSchedule) { return; }
    scheduler_update_time();

    task_t *old = current_task;
    task_t *next = scheduler_choose_next();

    if (next != old)
        current_task = next;
        current_process = current_task->parent_process;
    return;
}


// ONLY RUN WITH INTERRUPTS DISABLED!
task_t* alloc_task(){
    
    if (task_count >= MAX_TASKS) return NULL;


    task_t *t = kzalloc(sizeof(task_t)); 
    if(t==0){return 0;}
    memset(t, 0, sizeof(task_t));
    
    asm volatile ("cli");
    t->tid = tid_count;
    t->next = NULL;
    t->state = TASK_BLOCKED;
    if (!task_head) {

        task_head = t;
        t->next = t;
    } else {
        task_t *tail = task_head;
        int i= 0;
        while (tail->next != task_head) tail = tail->next;
        i++;
        tail->next = t;
        t->next = task_head;
        if(i>MAX_TASKS){
            panic("task queue exhaustion",0);
        }
    }

    task_count++;
    tid_count++;

    asm volatile("sti");
    return t;
}

// TODO include args in task_fn
task_t* create_ktask(task_fn fn, void *arg) {

    task_t* t = alloc_task();
    if(t==0){ return 0;}
     
    asm volatile("cli");
    uint32_t *sp = (uint32_t*)((uintptr_t)t->stack + KERNEL_STACK_SIZE);

    // set interrupt stack frame for interrupt return, after timer interrupt
    *(--sp) = INITIAL_EFLAGS;   // eflags
    *(--sp) = KERNEL_CODE_SELECTOR;  // cs
    *(--sp) = (uint32_t)fn;     // eip (entry point of the task)

    *(--sp) = 0x0; // eax
    *(--sp) = 0x0; // ecx
    *(--sp) = 0x0; // edx
    *(--sp) = 0x0; // ebx
    *(--sp) = 0x0; // esp (placeholder)
    *(--sp) = 0x0; // ebp
    *(--sp) = (uint32_t) arg; // esi (arg)
    *(--sp) = 0x0; // edi

    t->esp = sp;

    t->state = TASK_READY;

    

   
    
      asm volatile ("sti");
    
    return t;
}

task_t* create_ptask(process_t* proc, void* entry) {
   
    task_t* t = alloc_task();
    if(t==0){return 0;}

    asm volatile("cli");
    uint32_t *sp = (uint32_t*)((uintptr_t)t->stack + KERNEL_STACK_SIZE);

    // set interrupt stack frame for interrupt return, after timer interrupt
    *(--sp) = INITIAL_EFLAGS;   // eflags
    *(--sp) = KERNEL_CODE_SELECTOR;  // cs
    *(--sp) = (uint32_t)entry;     // eip (entry point of the task)

    *(--sp) = 0x0; // eax
    *(--sp) = 0x0; // ecx
    *(--sp) = 0x0; // edx
    *(--sp) = 0x0; // ebx
    *(--sp) = 0x0; // esp (placeholder)
    *(--sp) = 0x0; // ebp
    *(--sp) = (uint32_t) 0x0;//arg; // esi (arg)
    *(--sp) = 0x0; // edi

    t->esp = sp;

    t->state = TASK_READY;
    asm("sti");
    return t;
}

process_t* create_process(char* name /*elf_data*/, task_fn fn){
    
    // !! update to kernel heap (kzalloc)
    process_t* proc = kzalloc(sizeof(process_t));
    if(proc==NULL){return 0;}
    asm("cli");
    memset(proc, 0, sizeof(process_t));

    for(int i = 0; i < MAX_FD; i++)
    proc->fd_table[i] = NULL;

    proc->cwd = root_inode;
    inode_ref(root_inode);

    // proc->page_directory = create_page_directory();
    // 
    // load_elf(proc-page_directory, elf_data)
    // entry = elf_get_entry(elf_data);
    
    
    task_t* main_task = create_ptask(proc, (void*)fn);
    if(main_task == NULL){asm("sti");return 0;}

    main_task->parent_process = proc;
    proc->main_task = main_task;
    proc->pid = pid_count;

    process_count++;
    pid_count++;
    asm("cli");
    return proc;
}



task_t* create_idle(task_fn fn, void *arg) {
    return create_ktask((void*)idle_func, 0);
}

task_t* get_idle_task(){
    return idle_task;
}

void init_scheduler(){
    idle_task = create_idle((void*)idle_func,0);
    return;
}

void start_scheduling(void) {
    if (!task_head) return;
    CanSchedule = true;
    current_task = task_head;
    uint32_t *kernel_esp;
    serial_print("context switching..");
    context_switch(&kernel_esp, current_task->esp);
}

void stop_scheduling(void){
    asm volatile("cli");
    CanSchedule = false;
    asm volatile("sti");
}

