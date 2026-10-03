#include "types.h"
#include "task.h"
#include "vga-textmode.h"
#include "vmm.h"
#include "serial.h"
#include "tty.h"
#include "spinlock.h"
#include "inputsink.h"
#include "string.h"
#include "ufs.h"
#include "vfs.h"

#include "main.h"

#define MAX_TTY 255

void tty_handle_key_event(struct input_sink *self, struct key_event *ev);

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

tty_t* file_to_tty(struct file* file) {
    
    struct inode* inode = file->inode;
    if(!inode){ serial_print("no inode for tty file\n"); return NULL;}

    for (int i = 0; i < tty_count; i++){
        tty_t* tty = &tty_table[i];
        
        if (tty->tty_inode == inode){
            return tty;
        }
    }

    serial_print("no tty found for inode\n");
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


int ttyfile_write(struct file* file, const void* buf, size_t size){

    tty_t* tty = file_to_tty(file);
    if (!tty || (!buf && size != 0)) {
        serial_print("tty couldn't be resolved from file");
        return -1;
    }

    const char* bytes = (const char*)buf;
    for (size_t i = 0; i < size; i++) {
        tty_write(tty, bytes[i]);
    }

    return (int)size;
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
    tty->task_read_wait = current_task;

    while (count < maxlen)
    {
        while (tty->input_head == tty->input_tail) {
            tty->task_read_wait = current_task;
            block_task(current_task);
            if (tty->input_head != tty->input_tail) {
                wake_task(current_task);
            }
            if (current_task->state == TASK_BLOCKED) {
                __asm__ __volatile__("int $32");
            }
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
    if (tty->task_read_wait == current_task) {
        tty->task_read_wait = NULL;
    }
    return count;
}


int ttyfile_read(struct file* file, void* buf, size_t size){
    
    tty_t* tty = file_to_tty(file);
    if(!tty){
        serial_print("tty couldn't be resolved from file");
        return -1;
    }

    int ret = tty_read_line(tty, (char*)buf, size);
    return ret;
}



struct file_ops tty_file_ops = {
    .read = ttyfile_read,
    .write = ttyfile_write,
    .lseek = NULL,
    .open = NULL,
    .close = NULL
};


struct inode* create_tty_inode(uint32_t num){
    // prepare filename
    uint32_t ttyCountStrLen = (num / 10);
    if(ttyCountStrLen == 0){ttyCountStrLen = 1;}

    char filename[3 + ttyCountStrLen + 1];

    strcpy(filename, "tty");
    itoa((int)num, filename + 3, 10);
    
    char dirprefix[9+ttyCountStrLen];
    strcpy(dirprefix, "/dev/");
    strcpy(dirprefix+5, filename);

    int ret = sys_create(dirprefix);
    struct inode* inode = vfs_lookup(dirprefix, root_inode);

    if(!inode){
        serial_print("failed to inode file for: %s\n", dirprefix);
        return NULL;
    }

    inode->file_ops = &tty_file_ops;
    return inode;
}



tty_t* create_tty() {
    if (tty_count < MAX_TTY) {
        tty_t *tty = &tty_table[tty_count];

        struct inode* inode = create_tty_inode(tty_count);
        if(inode == NULL){
            serial_print("[create_tty]: no inode\n");
            return NULL;
        }

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

        tty->tty_inode = inode;
        create_input_sink(&tty->input_sink, tty_handle_key_event, NULL);

        serial_print("tty created (%d) inode at 0x%x inode file ops at 0x%x\n",tty_count, tty->tty_inode, tty->tty_inode->file_ops);

        tty_count++;
        return tty;
    }
    return NULL;
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
    tty_worker_task = create_ktask((void*)tty_worker, 0);
}