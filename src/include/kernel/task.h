
#ifndef TASK_H
#define TASK_H
#include "types.h"
#include "ufs.h"
#include "inputsink.h"
#include "vmm.h"


#define MAX_TASKS 512
#define MAX_PROCESS_TASKS 16
#define KERNEL_STACK_SIZE 4096
#define MAX_PROCESS_NAME_LEN 32

typedef void (*task_fn)(void *arg);
typedef struct tty tty_t;

extern volatile uint32_t usage_ticks;
extern volatile uint32_t busy_ticks;
extern volatile uint8_t usage;
extern volatile uint32_t system_ticks;


#define TASK_BLOCKED 0
#define TASK_SLEEPING 1
#define TASK_READY 2
#define TASK_RUNNING 3
#define TASK_EXITED 4

struct process;

typedef enum {
    TASK_KERNEL = 0,
    TASK_USER   = 3
} task_mode_t;




typedef struct task {
    uint32_t *esp;                 // saved kernel context stack pointer
    uint32_t tid;

    uint8_t stack[KERNEL_STACK_SIZE] __attribute__((aligned(16)));
    uint32_t kstack_top;           // top of kernel stack

    uint32_t user_stack_top;       // virtual top of user stack
    task_mode_t mode;              // kernel or user

    struct process* parent_process;

    uint8_t state;
    uint32_t wake_tick;

    tty_t* tty;
    struct task* next;
    struct task* qnext;
} task_t;

typedef struct process {
    char name[MAX_PROCESS_NAME_LEN]; // should be dynamically allocated
    task_t* main_task;
    task_t* task_table[MAX_PROCESS_TASKS]; // should be dynamically allocated
    struct input_sink input_sink;
    uint32_t pid;

    vm_env_t *vm_env;
    
    file_t* fd_table[MAX_FD];
    struct inode* cwd;
} process_t;



task_t* scheduler_choose_next(void);
void scheduler_tick(void);


task_t* create_ktask(task_fn fn, void *arg);

// not ready (
task_t* create_ptask(process_t* proc, void* entry);
process_t* create_process(char* name /*elf_data*/, task_fn fn);
process_t* create_process_from_elf(char* name, char* path, char* workingDirectory);
// )

void start_scheduling(void);
void stop_scheduling(void);
void task_exit(task_t *task);
void process_exit(process_t* process);
void trigger_schedule();

uint32_t get_task_table(task_t* tasktable);
uint32_t get_task_count();


task_t* PID_to_task(uint32_t pid);

void block_task(task_t *task);
void wake_task(task_t *task);
void terminate_task(task_t *task);

void init_scheduler();
process_t* create_empty_kernel_process(char* name);

extern task_t *current_task;
extern process_t* current_process;
extern uint32_t task_count;
extern task_t *task_head;
extern task_t *idle_task;

extern uint32_t process_count;


#endif