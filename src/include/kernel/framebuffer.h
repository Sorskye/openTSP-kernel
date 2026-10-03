#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include "types.h"
#include "pmm.h" // for multiboot info
#include "pixel_format.h"



// after
struct framebuffer {
    uint32_t *addr;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    struct pixel_format fmt;
};




uint32_t fb_make_color(struct framebuffer fb, uint8_t r, uint8_t g, uint8_t b);
void framebuffer_init();
struct framebuffer get_framebuffer();
struct framebuffer allocate_framebuffer(struct framebuffer reference_framebuffer);

#endif