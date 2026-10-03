#pragma once
#ifndef SPINLOCK_H
#define SPINLOCK_H

#include "task.h"

typedef struct {
    volatile int locked;
    task_t* owner;
} spinlock_t;

void spinlock_acquire(spinlock_t *lock);
void spinlock_release(spinlock_t *lock);
#endif