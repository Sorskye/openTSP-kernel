#include "types.h"
#include "task.h"
#include "vga-textmode.h"
#include "memory.h"
#include "serial.h"
#include "tty.h"
#include "spinlock.h"
#include "inputsink.h"

#define MAX_TTY 255

tty_t tty_table[MAX_TTY];
int tty_count = 0;
static tty_t* active_tty = {0};
task_t* tty_worker_task;


tty_t* get_active_tty(){
    return active_tty;
}

void set_active_tty(tty_t* tty){
    active_tty = tty;
}

void tty_handle_key_event(struct input_sink *self, struct key_event *ev) {
    
    tty_t* tty = (tty_t*)self;
    if (ev->pressed && is_keycode_char(ev->code)) {
        struct kbd_state state;
        get_kbd_state(&state);
    
        char c = keycode_to_char(ev->code, state);
        if(c==0){return;}

        int next_head = (tty->raw_input_head + 1) %TTY_INPUT_BUFF_SIZE;
        if (next_head != tty->raw_input_tail) {
            tty->raw_input_buff[tty->raw_input_head] = c;
            tty->raw_input_head = next_head;
            wake_task(tty->task_worker_wait);
        }
    }
}

tty_t* create_tty() {
    if (tty_count < MAX_TTY) {
        tty_t *tty = &tty_table[tty_count];
        
        create_input_sink(&tty->input_sink, (void*)tty_handle_key_event, 0);
        

        tty->input_lock.locked  = 0;
        tty->output_lock.locked = 0;

        tty->input_head  = 0;
        tty->input_tail  = 0;
        tty->output_head = 0;
        tty->output_tail = 0;

        tty->task_backend_wait = NULL;
        tty->task_read_wait = NULL;
        tty->task_worker_wait = NULL;

        tty->echo  = 1;
        tty->count = 0;
        tty->id = tty_count;
        tty_count++;

        return tty;
    }
    return NULL;
}




void tty_write(tty_t* tty, char c) {
    while (1) {
        spinlock_acquire(&tty->output_lock);

        int next_head = (tty->output_head + 1) % TTY_OUTPUT_BUFF_SIZE;

        if (next_head != tty->output_tail) {
            tty->output_buff[tty->output_head] = c;
            tty->output_head = next_head;

            if (tty->task_backend_wait) {
                wake_task(tty->task_backend_wait);
            }

            spinlock_release(&tty->output_lock);
            return;
        }

        spinlock_release(&tty->output_lock);

        // yield CPU so backend can run
        asm volatile ("int $32");
    }
}


void tty_write_line(tty_t* tty, const char* string) {

    while (*string) {
        tty_write(tty, *string);
        string++; 
    }
    return;
}


char tty_read(tty_t* tty)
{   
    while (tty->input_head == tty->input_tail)
    {   
        block_task(current_task);
        __asm__ __volatile__("int $32");
    }
    
    spinlock_acquire(&tty->input_lock);
    char c = tty->input_buff[tty->input_tail];
    tty->input_tail = (tty->input_tail + 1) % TTY_INPUT_BUFF_SIZE;
    spinlock_release(&tty->input_lock);

    return c;
}


int tty_read_line(tty_t* tty, char *buffer, int maxlen)
{
    int count = 0;

    while (count < maxlen)
    {
        while (tty->input_head == tty->input_tail) {
            block_task(current_task);  
            __asm__ __volatile__("int $32");
        }
        spinlock_acquire(&tty->input_lock);
        char c = tty->input_buff[tty->input_tail];
        tty->input_tail = (tty->input_tail + 1) % TTY_INPUT_BUFF_SIZE;
        buffer[count++] = c;
        spinlock_release(&tty->input_lock);

        if (c == '\n')  
            break;
    }
   
    buffer[count] = '\0';
    return count;
}



void tty_worker(void) {
    tty_t* tty = active_tty; 
    char edit_buffer[256];  // Line being edited
    int edit_pos = 0;
    
    while (1) {
        if (tty->raw_input_head == tty->raw_input_tail) {
            tty->task_worker_wait = current_task;
            block_task(current_task);
            __asm__ __volatile__("int $32");
        }
        
        while (tty->raw_input_head != tty->raw_input_tail) {
            spinlock_acquire(&tty->raw_input_lock);
            char c = tty->raw_input_buff[tty->raw_input_tail];
            tty->raw_input_tail = (tty->raw_input_tail + 1) % TTY_INPUT_BUFF_SIZE;
            spinlock_release(&tty->raw_input_lock);

            // Handle backspace
            if (c == '\b') {
                if (edit_pos > 0) {
                    edit_pos--;
                    // Echo backspace sequence: backspace, space, backspace
                    if (tty->echo == 1) {
                        tty_write(tty, '\b');
                        tty_write(tty, ' ');
                        tty_write(tty, '\b');
                    }
                }
                continue;
            }

            // Handle newline - send completed line to input buffer
            if (c == '\n' || c == '\r') {
                spinlock_acquire(&tty->input_lock);
                
                // Write all buffered characters
                for (int i = 0; i < edit_pos; i++) {
                    int next_head = (tty->input_head + 1) % TTY_INPUT_BUFF_SIZE;
                    if (next_head != tty->input_tail) {
                        tty->input_buff[tty->input_head] = edit_buffer[i];
                        tty->input_head = next_head;
                    }
                }
                
                // Write newline
                int next_head = (tty->input_head + 1) % TTY_INPUT_BUFF_SIZE;
                if (next_head != tty->input_tail) {
                    tty->input_buff[tty->input_head] = '\n';
                    tty->input_head = next_head;
                }
                
                spinlock_release(&tty->input_lock);
                
                // Echo the newline
                if (tty->echo == 1) {
                    tty_write(tty, '\n');
                }
                
                // Reset edit buffer for next line
                edit_pos = 0;
                
                // Wake up reader
                if (tty->task_read_wait) {
                    wake_task(tty->task_read_wait);
                }
                continue;
            }

            // Regular character - add to edit buffer
            if (edit_pos < 255) {
                edit_buffer[edit_pos++] = c;
                
                // Echo the character
                if (tty->echo == 1) {
                    tty_write(tty, c);
                }
            }
        }
        
        if (tty->task_read_wait) {
            wake_task(tty->task_read_wait);
        }
    }
}

void tty_init(){
    task_t* tty_worker_task = create_ktask((void*)tty_worker,0);
}