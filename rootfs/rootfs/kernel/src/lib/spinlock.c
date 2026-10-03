#include "types.h"
#include "spinlock.h"
#include "serial.h"
#include "task.h"

static inline int xchg(volatile int *addr, int newval) {
    int old;
    __asm__ __volatile__(
        "xchg %0, %1"
        : "=r"(old), "+m"(*addr)
        : "0"(newval)
        : "memory"
    );
    return old;
}

void spinlock_acquire(spinlock_t *lock){

    uint32_t count = 0;
    while(xchg(&lock->locked, 1)){
        if(count > 999999){serial_print("SPINLOCK INPATIENCE\n"); count = 0;}
        count++;
        __asm__ __volatile__("pause");
    }
    
    lock->owner = current_task;
   // serial_print("spinlock acquired: ");
   // serial_print(current_task->taskname);
   // serial_print("\n");
    return;
}

void spinlock_release(spinlock_t *lock){
    __asm__ __volatile__("" ::: "memory");

    lock->locked = 0;
    //serial_print("spinlock released: ");
   // serial_print(lock->owner->taskname);
   // serial_print("\n");
    return;
}
