#ifndef INPUT_H
#define INPUT_H

#include "types.h"

enum keycode {
    KEY_none = 0,

    // Letters
    KEY_a,
    KEY_b,
    KEY_c,
    KEY_d,
    KEY_e,
    KEY_f,
    KEY_g,
    KEY_h,
    KEY_i,
    KEY_j,
    KEY_k,
    KEY_l,
    KEY_m,
    KEY_n,
    KEY_o,
    KEY_p,
    KEY_q,
    KEY_r,
    KEY_s,
    KEY_t,
    KEY_u,
    KEY_v,
    KEY_w,
    KEY_x,
    KEY_y,
    KEY_z,
    

    // Numbers
    KEY_0,
    KEY_1,
    KEY_2,
    KEY_3,
    KEY_4,
    KEY_5,
    KEY_6,
    KEY_7,
    KEY_8,
    KEY_9,

    // Function keys
    KEY_f1,
    KEY_f2,
    KEY_f3,
    KEY_f4,
    KEY_f5,
    KEY_f6,
    KEY_f7,
    KEY_f8,
    KEY_f9,
    KEY_f10,
    KEY_f11,
    KEY_f12,

    // symbols
    KEY_dot,
    KEY_slash,
    KEY_backslash,

    // Control keys
    KEY_escape,
    KEY_tab,
    KEY_capslock,
    KEY_lshift,
    KEY_rshift,
    KEY_lctrl,
    KEY_rctrl,
    KEY_lalt,
    KEY_ralt,
    KEY_space,
    KEY_enter,
    KEY_backspace,

    // Navigation
    KEY_up,
    KEY_down,
    KEY_left,
    KEY_right,

    // Misc
    KEY_insert,
    KEY_delete,
    KEY_home,
    KEY_end,
    KEY_pageup,
    KEY_pagedown,
    KEY_cmd,
};


struct key_event{
    enum keycode code;
    bool pressed; 
};

struct mouse_event {
    int     dx;
    int     dy;
    int     scroll_delta;
    uint8_t buttons;           // full held state — check this for drag
    uint8_t pressed_buttons;   // edges: newly pressed this packet
    uint8_t released_buttons;  // edges: newly released this packet
    bool    pressed;           // true if any button newly pressed
};


struct kbd_state {


    // Left/right variants (important at OS level)
    bool shift;
    bool lshift;
    bool rshift;

    bool ctrl;
    bool lctrl;
    bool rctrl;

    bool alt;
    bool lalt;
    bool ralt;

    bool meta;
    bool lmeta;
    bool rmeta;

    // Toggle keys
    bool capslock;
    bool numlock;
    bool scrolllock;
};


struct input_sink {
    void (*handle_key_event)(struct input_sink *self, struct key_event *ev);
    void (*handle_mouse_event)(struct input_sink *self, struct mouse_event *ev);
};

//api for input sinks
void get_kbd_state(struct kbd_state *state);
void update_kbd_state(struct key_event *ev);

void get_active_sink(struct input_sink **out);
void set_active_sink(struct input_sink *sink);
void create_input_sink(struct input_sink *sink, void (*handle_key_event)(struct input_sink *, struct key_event *), void (*handle_mouse_event)(struct input_sink *, struct mouse_event *));

char keycode_to_char(enum keycode key, struct kbd_state state);
bool is_keycode_char(enum keycode code);

void handle_key_event(struct key_event *ev);
void handle_mouse_event(struct mouse_event *ev);

#endif