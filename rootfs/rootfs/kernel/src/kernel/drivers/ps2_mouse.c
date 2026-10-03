#include "types.h"
#include "ps2_mouse.h"
#include "io.h"
#include "spinlock.h"
#include "serial.h"
#include "inputsink.h"
#include "task.h"
#include "ringbuf.h"

static spinlock_t mouse_lock = {0};


static uint8_t mouse_cycle = 0;
static uint8_t mouse_bytes[4];  // must be 4 to hold wheel packet

static uint8_t mouse_storage[128];
static ringbuf_t mouse_buf;

task_t* mouse_handler_task = {0};

static bool mouse_has_wheel = false;
static bool mouse_has_5buttons = false;

static struct mouse_event build_mouse_event(void)
{
    struct mouse_event ev = {0};

    ev.dx = (int8_t)mouse_bytes[1];
    ev.dy = -(int8_t)mouse_bytes[2];

    // Build full button state from this packet
    uint8_t new_buttons = 0;
    if (mouse_bytes[0] & 0x01) new_buttons |= MOUSE_BTN_LEFT;
    if (mouse_bytes[0] & 0x02) new_buttons |= MOUSE_BTN_RIGHT;
    if (mouse_bytes[0] & 0x04) new_buttons |= MOUSE_BTN_MIDDLE;

    if (mouse_has_wheel) {
        int8_t wheel = (int8_t)(mouse_bytes[3] & 0x0F);
        if (wheel & 0x08) wheel |= 0xF0;
        ev.scroll_delta = wheel;

        if (mouse_has_5buttons) {
            if (mouse_bytes[3] & 0x10) new_buttons |= MOUSE_BTN_4;
            if (mouse_bytes[3] & 0x20) new_buttons |= MOUSE_BTN_5;
        }
    }

    // Sanity check: if dx/dy are both 0 and buttons didn't change
    // and scroll is 0, this is likely a corrupt/resynced packet — discard
    static uint8_t last_buttons = 0;

    ev.buttons          = new_buttons;
    ev.pressed_buttons  =  new_buttons & ~last_buttons;
    ev.released_buttons = ~new_buttons &  last_buttons;
    ev.pressed          = (ev.pressed_buttons != 0);
    last_buttons        = new_buttons;

    return ev;
}

struct mouse_event mousedata_to_mouse_event(uint8_t data)
{
    struct mouse_event empty = {0};

    switch (mouse_cycle) {
        case 0:
            // Overflow bits (bit 6 = X overflow, bit 7 = Y overflow)
            // being set simultaneously usually means we're out of sync
            if ((data & 0xC0) == 0xC0) {
                mouse_cycle = 0;  // stay unsynced
                return empty;
            }
            mouse_bytes[0] = data;
            mouse_cycle = 1;
            return empty;

        case 1:
            mouse_bytes[1] = data;
            mouse_cycle = 2;
            return empty;

        case 2:
            mouse_bytes[2] = data;
            if (mouse_has_wheel) {
                mouse_cycle = 3;
                return empty;
            }
            mouse_cycle = 0;
            return build_mouse_event();

        case 3:
            mouse_bytes[3] = data;
            mouse_cycle = 0;
            return build_mouse_event();
    }

    return empty;
}

void ps2mouse_handler_thread(){
    while(1){
        uint8_t mousedata;
        while (!ringbuf_pop(&mouse_buf, &mousedata)) {
            current_task->state = TASK_BLOCKED;
            asm __volatile__("int $32");
        }

        struct mouse_event ev = mousedata_to_mouse_event(mousedata);
        handle_mouse_event(&ev);
      
    }
}

void mouse_wait(uint8_t type) {
    // type = 0 → wait for data
    // type = 1 → wait for input buffer to be clear
    uint32_t timeout = 100000;
    if (type == 0) {
        while (timeout--) {
            if (inb(0x64) & 1) return;
        }
    } else {
        while (timeout--) {
            if (!(inb(0x64) & 2)) return;
        }
    }
}

void mouse_write(uint8_t value) {
    mouse_wait(1);
    outb(0x64, 0xD4);
    mouse_wait(1);
    outb(0x60, value);
}

uint8_t mouse_read() {
    mouse_wait(0);
    return inb(0x60);
}

// a bit hacky to clear interrupts but i have to rewrite this anyway
void mouse_init() {
    serial_print("Initializing PS/2 mouse...\n");
    uint8_t status;

    mouse_wait(1);
    outb(0x64, 0xA8); // enable auxiliary device

    mouse_wait(1);
    outb(0x64, 0x20); // read command byte
    mouse_wait(0);
    status = inb(0x60);

    status |= 2; // enable mouse IRQ

    mouse_wait(1);
    outb(0x64, 0x60);
    mouse_wait(1);
    outb(0x60, status);

    // Reset mouse to defaults
    mouse_write(0xF6);
    mouse_read(); // ACK

    // Enable data reporting
    mouse_write(0xF4);
    mouse_read(); // ACK

    //
    // --- IntelliMouse wheel detection sequence ---
    //

    // Set sample rate 200
    mouse_write(0xF3);
    mouse_read();
    mouse_write(200);
    mouse_read();

    // Set sample rate 100
    mouse_write(0xF3);
    mouse_read();
    mouse_write(100);
    mouse_read();

    // Set sample rate 80
    mouse_write(0xF3);
    mouse_read();
    mouse_write(80);
    mouse_read();

    // Request mouse ID
    mouse_write(0xF2);
    mouse_read(); // ACK
    uint8_t mouse_id = mouse_read();

    if (mouse_id == 0x03) {
        mouse_has_wheel = true;
        serial_print("Mouse: IntelliMouse wheel detected\n");
    } else if (mouse_id == 0x04) {
        mouse_has_wheel = true;
        mouse_has_5buttons = true;
        serial_print("Mouse: IntelliMouse Explorer (5 buttons) detected\n");
    } else {
        serial_print("Mouse: Standard PS/2 mouse detected\n");
    }

    //
    // --- Start your handler thread ---
    //
    ringbuf_init(&mouse_buf, mouse_storage, sizeof(mouse_storage));
    mouse_handler_task = create_ktask((void*)ps2mouse_handler_thread, 0);

    serial_print("PS/2 mouse initialized\n");
}


// interrupt context
void mouse_irq_handler() {
    uint8_t status = inb(0x64);
    if (!(status & 0x20))
        return; // not mouse data

    uint8_t data = inb(0x60);
    ringbuf_push(&mouse_buf, data);
    if(mouse_handler_task->tid != 0 && mouse_handler_task->state == TASK_BLOCKED){
        mouse_handler_task->state = TASK_READY;
    }
    return;

}