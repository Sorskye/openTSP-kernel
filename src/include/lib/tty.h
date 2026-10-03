#pragma once
#ifndef TTY_H
#define TTY_H

#include "types.h"
#include "spinlock.h"
#include "inputsink.h"
#include "ufs.h"

#define TTY_INPUT_BUFF_SIZE 1024
#define TTY_OUTPUT_BUFF_SIZE 1024

typedef struct tty tty_t;
void init_tty();
extern task_t* tty_worker_task;

typedef struct tty{
    struct input_sink input_sink;

    volatile char raw_input_buff[TTY_INPUT_BUFF_SIZE]; 
    volatile int raw_input_head;
    volatile int raw_input_tail;

    volatile char input_buff[TTY_INPUT_BUFF_SIZE];
    volatile int input_head;
    volatile int input_tail;

    volatile char output_buff[TTY_OUTPUT_BUFF_SIZE];
    volatile int output_head;
    volatile int output_tail;

    int count; 
    int echo;
    int id;
    task_t* task_read_wait;
    task_t* task_backend_wait;
    task_t* task_worker_wait;
    spinlock_t raw_input_lock;
    spinlock_t input_lock;
    spinlock_t output_lock;

    struct inode *tty_inode;
} tty_t;



void tty_write(tty_t* tty, const char c);
void tty_write_line(tty_t* tty, const char* string);
char tty_read(tty_t* tty);
int tty_read_line(tty_t* tty, char *buffer, int maxlen);

tty_t* get_active_tty();
void set_active_tty(tty_t* tty);

void tty_init();
tty_t* create_tty();

#endif