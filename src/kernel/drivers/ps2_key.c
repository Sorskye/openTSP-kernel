#include "types.h"
#include "task.h"
#include "vmm.h"
#include "ps2_key.h"
#include "IDT.h"
#include "io.h"
#include "debug.h"

#include "serial.h"

#include "ringbuf.h"
#include "inputsink.h"


static uint8_t kbd_storage[128];
static ringbuf_t kbd_buf;
static bool key_down[128];


// task context
struct key_event scancode_to_keyevent(uint8_t sc)
{
    static bool extended = false;
    struct key_event ev = { KEY_none, false };

    if (sc == 0xE0) {
        extended = true;
        return ev;
    }

    bool released = (sc & 0x80) != 0;
    uint8_t code = sc & 0x7F;
    ev.pressed = !released;
    if (!extended) {
        switch (code) {
            // Letters
            case 0x1E: ev.code = KEY_a; break;
            case 0x30: ev.code = KEY_b; break;
            case 0x2E: ev.code = KEY_c; break;
            case 0x20: ev.code = KEY_d; break;
            case 0x12: ev.code = KEY_e; break;
            case 0x21: ev.code = KEY_f; break;
            case 0x22: ev.code = KEY_g; break;
            case 0x23: ev.code = KEY_h; break;
            case 0x17: ev.code = KEY_i; break;
            case 0x24: ev.code = KEY_j; break;
            case 0x25: ev.code = KEY_k; break;
            case 0x26: ev.code = KEY_l; break;
            case 0x32: ev.code = KEY_m; break;
            case 0x31: ev.code = KEY_n; break;
            case 0x18: ev.code = KEY_o; break;
            case 0x19: ev.code = KEY_p; break;
            case 0x10: ev.code = KEY_q; break;
            case 0x13: ev.code = KEY_r; break;
            case 0x1F: ev.code = KEY_s; break;
            case 0x14: ev.code = KEY_t; break;
            case 0x16: ev.code = KEY_u; break;
            case 0x2F: ev.code = KEY_v; break;
            case 0x11: ev.code = KEY_w; break;
            case 0x2D: ev.code = KEY_x; break;
            case 0x15: ev.code = KEY_y; break;
            case 0x2C: ev.code = KEY_z; break;

            // Numbers
            case 0x0B: ev.code = KEY_0; break;
            case 0x02: ev.code = KEY_1; break;
            case 0x03: ev.code = KEY_2; break;
            case 0x04: ev.code = KEY_3; break;
            case 0x05: ev.code = KEY_4; break;
            case 0x06: ev.code = KEY_5; break;
            case 0x07: ev.code = KEY_6; break;
            case 0x08: ev.code = KEY_7; break;
            case 0x09: ev.code = KEY_8; break;
            case 0x0A: ev.code = KEY_9; break;

            // Function keys
            case 0x3B: ev.code = KEY_f1; break;
            case 0x3C: ev.code = KEY_f2; break;
            case 0x3D: ev.code = KEY_f3; break;
            case 0x3E: ev.code = KEY_f4; break;
            case 0x3F: ev.code = KEY_f5; break;
            case 0x40: ev.code = KEY_f6; break;
            case 0x41: ev.code = KEY_f7; break;
            case 0x42: ev.code = KEY_f8; break;
            case 0x43: ev.code = KEY_f9; break;
            case 0x44: ev.code = KEY_f10; break;
            case 0x57: ev.code = KEY_f11; break;
            case 0x58: ev.code = KEY_f12; break;

            // Control keys
            case 0x01: ev.code = KEY_escape; break;
            case 0x0F: ev.code = KEY_tab; break;
            case 0x3A: ev.code = KEY_capslock; break;
            case 0x2A: ev.code = KEY_lshift; break;
            case 0x36: ev.code = KEY_rshift; break;
            case 0x1D: ev.code = KEY_lctrl; break;
            case 0x38: ev.code = KEY_lalt; break;
            case 0x39: ev.code = KEY_space; break;
            case 0x1C: ev.code = KEY_enter; break;
            case 0x0E: ev.code = KEY_backspace; break;
            
            case 0x34: ev.code = KEY_dot; break;
            case 0x35: ev.code = KEY_slash; break;
            case 0x2B: ev.code = KEY_backslash; break;
         
            case 0x0C: ev.code = KEY_dash; break;

            default: ev.code = KEY_none; break;
        }
    } else {
        switch (code) {
            case 0x1D: ev.code = KEY_rctrl; break;
            case 0x38: ev.code = KEY_ralt; break;

            case 0x48: ev.code = KEY_up; break;
            case 0x50: ev.code = KEY_down; break;
            case 0x4B: ev.code = KEY_left; break;
            case 0x4D: ev.code = KEY_right; break;

            case 0x52: ev.code = KEY_insert; break;
            case 0x53: ev.code = KEY_delete; break;
            case 0x47: ev.code = KEY_home; break;
            case 0x4F: ev.code = KEY_end; break;
            case 0x49: ev.code = KEY_pageup; break;
            case 0x51: ev.code = KEY_pagedown; break;

            case 0x5B: ev.code = KEY_cmd; break; // left GUI
            case 0x5C: ev.code = KEY_cmd; break; // right GUI

            default: ev.code = KEY_none; break;
        }

        extended = false;
    }

    return ev;
}

void ps2keyboard_handler_thread(){
    while(1){
        uint8_t scancode;
        while (!ringbuf_pop(&kbd_buf, &scancode)) {
            current_task->state = TASK_BLOCKED;
            asm __volatile__("int $32");
        }

        struct key_event ev = scancode_to_keyevent(scancode);
        if (ev.code != KEY_none) {
            if (ev.pressed) {
                if (key_down[ev.code]) {
                    continue;
                }
                key_down[ev.code] = true;
            } else {
                key_down[ev.code] = false;
            }

            handle_key_event(&ev);
        }
    }
}

task_t* dispatch_task;
void ps2keyboard_init() {
    ringbuf_init(&kbd_buf, kbd_storage, sizeof(kbd_storage));
    dispatch_task = create_ktask((void*)ps2keyboard_handler_thread, 0);
    serial_print("PS/2 keyboard initialized\n");
    // set IRQ handler for keyboard (IRQ1)
}

// irq context 
void ps2keyboard_irq(){
    uint8_t scancode = inb(0x60);
    ringbuf_push(&kbd_buf, scancode);
    if(dispatch_task->state == TASK_BLOCKED){
        dispatch_task->state = TASK_READY;
    }
    return;
}

