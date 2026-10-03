#include "inputsink.h"
#include "types.h"
#include "spinlock.h"
#include "memory.h"
#include "string.h"
#include "serial.h"
#include "io.h"
#include "pit.h"

#include "stdio.h"


static struct input_sink *active_sink = NULL;
static struct kbd_state kbd_state = {0};


static spinlock_t input_focus_lock = {0};
void get_active_sink(struct input_sink **out) {
    spinlock_acquire(&input_focus_lock);
    memcpy(out, &active_sink, sizeof(struct input_sink*));
    spinlock_release(&input_focus_lock);
}

void create_input_sink(struct input_sink *sink, void (*handle_key_event)(struct input_sink *, struct key_event *), void (*handle_mouse_event)(struct input_sink *, struct mouse_event *)) {
    sink->handle_key_event = handle_key_event;
    sink->handle_mouse_event = handle_mouse_event;
}

void set_active_sink(struct input_sink *sink) {
    spinlock_acquire(&input_focus_lock);
    active_sink = sink;
    spinlock_release(&input_focus_lock);
}

spinlock_t kbd_state_lock = {0};
void get_kbd_state(struct kbd_state *state) {
    spinlock_acquire(&kbd_state_lock);
    memcpy(state, &kbd_state, sizeof(struct kbd_state));
    spinlock_release(&kbd_state_lock);
}
void set_kbd_state(struct kbd_state *state) {
    spinlock_acquire(&kbd_state_lock);
    memcpy(&kbd_state, state, sizeof(struct kbd_state));
    spinlock_release(&kbd_state_lock);
}
void update_kbd_state(struct key_event *ev) {
    struct kbd_state new_state;
    get_kbd_state(&new_state);

    bool pressed = ev->pressed;

    switch (ev->code) {
        case KEY_lshift:
            new_state.lshift = pressed;
            new_state.shift = pressed;
            break;
        case KEY_rshift:
            new_state.rshift = pressed;
            new_state.shift = pressed;
            break;
        case KEY_lctrl:
            new_state.lctrl = pressed;
            new_state.ctrl = pressed;
            break;
        case KEY_rctrl:
            new_state.rctrl = pressed;
            new_state.ctrl = pressed;
            break;
        case KEY_lalt:
            new_state.lalt = pressed;
            new_state.alt = pressed;
            break;
        case KEY_ralt:
            new_state.ralt = pressed;
            new_state.alt = pressed;
            break;
        case KEY_cmd:
            new_state.lmeta = pressed; // Treat cmd as left meta
            new_state.meta = pressed;
            break;
        case KEY_capslock:
            if (pressed) // Toggle on key press
                new_state.capslock = !new_state.capslock;
            break;
        default:
            break; // Other keys don't affect state
    }

    // Update combined states
    new_state.shift = new_state.lshift || new_state.rshift;
    new_state.ctrl  = new_state.lctrl  || new_state.rctrl;
    new_state.alt   = new_state.lalt   || new_state.ralt;
    new_state.meta  = new_state.lmeta  || new_state.rmeta;

    set_kbd_state(&new_state);
}



void dispatch_key_event(struct key_event *ev) {
    struct input_sink *active_sink;
    get_active_sink(&active_sink);
    if (active_sink && active_sink->handle_key_event) {
        active_sink->handle_key_event(active_sink, ev);
    }
}

void dispatch_mouse_event(struct mouse_event *ev) {
    struct input_sink *active_sink;
    get_active_sink(&active_sink);
    if (active_sink && active_sink->handle_mouse_event) {
        active_sink->handle_mouse_event(active_sink, ev);
    }
}

bool is_keycode_char(enum keycode code) {
    return (code >= KEY_a && code <= KEY_z) || 
           (code >= KEY_0 && code <= KEY_9) ||
           code == KEY_space || code == KEY_enter || code == KEY_tab || code == KEY_backspace || code == KEY_dot || code == KEY_slash;
}

char keycode_to_char(enum keycode key, struct kbd_state state)
{

    if (key >= KEY_a && key <= KEY_z) {
        char base = state.shift ? 'A' : 'a';
        return base + (key - KEY_a);
    }

    if (key >= KEY_0 && key <= KEY_9) {
        const char normal[] = {'0','1','2','3','4','5','6','7','8','9'};
        const char shifted[] = {')','!','@','#','$','%','^','&','*','('};
        return state.shift ? shifted[key - KEY_0] : normal[key - KEY_0];
    }


    switch(key){
        case KEY_dot:
        return '.';
        break;

        case KEY_slash:
        return '/';
        break;

        case KEY_backslash:
        return '\\';
        break;

        case KEY_space:
        return ' ';
        break;
        
        case KEY_enter:
        return '\n';
        break;

        case KEY_backspace:
        return '\b';
        break;

        case KEY_tab:
        return '\t';
        break;

        default:
        return 0;

    }



    return 0;
}

void handle_key_event(struct key_event *ev) {
    // ctrl+alt+del to reboot
    struct kbd_state state;
    get_kbd_state(&state);

    if (ev->code == KEY_r && ev->pressed) {
        if (state.ctrl && state.alt) {
            serial_print("REBOOT!");
            outb(0x64, 0xFE);
            return;
        }
    }else if(ev->code == KEY_f1 && ev->pressed){
        printf("hello!");
    }else if(ev->code == KEY_1 && ev->pressed){
        if(state.ralt){
            uint32_t freq = get_pit_frequency();
            pit_init(freq+=5);
            serial_print("pit frequency: %d hz\n",get_pit_frequency());
            return;
        }
    }else if(ev->code == KEY_2 && ev->pressed){
        if(state.ralt){
            uint32_t freq = get_pit_frequency();
            pit_init(freq-=5);
            serial_print("pit frequency: %d hz\n",get_pit_frequency());
            printf("TEST");
            return;
        }
    }

    if(is_keycode_char(ev->code) && ev->pressed){
        serial_print("%c", keycode_to_char(ev->code, state));
    }

    update_kbd_state(ev);
    dispatch_key_event(ev);
}



void handle_mouse_event(struct mouse_event *ev) {
    dispatch_mouse_event(ev);
}
