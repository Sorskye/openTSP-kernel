#include "pit.h"
#include "task.h"
#include "kerror.h"

extern void scheduler_switch_now(void);

static inline uint32_t ms_to_ticks(uint32_t ms) {
    uint32_t PIT_FREQUENCY = get_pit_frequency();
    return (ms * PIT_FREQUENCY + 999) / 1000;
}



void sleep_ms(uint32_t ms) {
    
    uint32_t ticks = ms_to_ticks(ms);
    if (ticks == 0) ticks = 1; 

    asm("cli");
    current_task->wake_tick = system_ticks + ticks;
    current_task->state = TASK_SLEEPING;
    asm("sti");
    asm volatile ("int $32");
    return;
}
