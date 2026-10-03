#ifndef PS2_MOUSE_H
#define PS2_MOUSE_H
    #include "types.h"

    #define MOUSE_BTN_LEFT   (1 << 0)
    #define MOUSE_BTN_RIGHT  (1 << 1)
    #define MOUSE_BTN_MIDDLE (1 << 2)
    #define MOUSE_BTN_4      (1 << 3)
    #define MOUSE_BTN_5      (1 << 4)

    void mouse_init();
    void mouse_irq_handler();
    
#endif