#pragma once
#ifndef COMPOSITOR_H
#define COMPOSITOR_H

#include "types.h"
#include "task.h"
#include "video.h"
#include "psf.h"

#define WINDOW_TITLE_MAX_LENGTH 255

#define MOUSE_BTN_LEFT   (1 << 0)
#define MOUSE_BTN_RIGHT  (1 << 1)
#define MOUSE_BTN_MIDDLE (1 << 2)
#define MOUSE_BTN_4      (1 << 3)
#define MOUSE_BTN_5      (1 << 4)


typedef struct window_border_theme {
    struct image* tl;
    struct image* t;
    struct image* tr;
    struct image* l;
    struct image* r;
    struct image* bl;
    struct image* b;
    struct image* br;

    int corner_w;
    int corner_h;

    psf_font_t font;
} window_border_theme_t;


typedef struct window_params{
    char title[WINDOW_TITLE_MAX_LENGTH];
    struct color_rgba bar_image_colorkey;
    bool is_resizable;

}window_params_t;


typedef struct {
    int x, y;           // position relative to window content area
    int abs_x, abs_y;   // absolute screen position
    uint8_t buttons;    // MOUSE_BTN_* bitmask of currently held buttons
} window_mouse_event_t;



typedef enum {
    WINDOW_MOUSE_ENTER,
    WINDOW_MOUSE_LEAVE,
    WINDOW_MOUSE_MOVE,
    WINDOW_MOUSE_BTN_DOWN,
    WINDOW_MOUSE_BTN_UP,
    WINDOW_MOUSE_SCROLL,
} window_mouse_event_type_t;

typedef struct {
    window_mouse_event_type_t type;
    window_mouse_event_t      mouse;
    int                       scroll_delta;  // for WINDOW_MOUSE_SCROLL
    uint8_t                   button;        // which button for BTN_DOWN/UP
} window_event_t;



typedef struct window {
    struct image *image;
    struct image *bar_image;
    uint16_t width;
    uint16_t height;
    int prevx;
    int prevy;
    int posx;
    int posy;
    bool dirty;
    uint8_t z;
    window_params_t *window_params;
    process_t *owner_process;
    bool visible;

    bool mouse_inside;     // is cursor currently over this window?
   // window_event_fn_t  on_event;         // optional event callback
} window_t;

//typedef void (*window_event_fn_t)(window_t *win, window_event_t *ev);

void compositor_main();
void compositor_clear_screen();


#endif