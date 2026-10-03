#ifndef VIDEO_H
#define VIDEO_H

#include "types.h"
#include "framebuffer.h"
#include "pixel_format.h"



struct color_rgba {
    uint8_t r, g, b, a;
};



struct image {
    void *addr;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    struct pixel_format fmt;
    bool owns_memory;
};

typedef struct rect{
    int x;
    int y;
    int w;
    int h;

}rect_t;

enum blit_flags {
    BLIT_NONE      = 0,
    BLIT_COLORKEY  = 1 << 0
};

struct blit_opts {
    uint32_t flags;
    struct color_rgba colorkey;
};

/* Creation / destruction */
struct image *image_create(uint32_t width, uint32_t height, const struct pixel_format *fmt);
struct image *image_clone(const struct image *src);
void image_destroy(struct image *img);

/* Wrapping existing memory (for hardware framebuffer etc.) */
struct image image_wrap(void *addr, uint32_t width, uint32_t height, uint32_t pitch, const struct pixel_format *fmt);

/* Pixel/color helpers */
uint32_t image_make_color(const struct image *img, uint8_t r, uint8_t g, uint8_t b);
void image_put_pixel(struct image *img, uint32_t x, uint32_t y, struct color_rgba color);

void image_clear(struct image *img, struct color_rgba color);
//rect
bool rect_intersect(rect_t a, rect_t b, rect_t *out);
int rect_subtract(rect_t src, rect_t cut, rect_t out[4]);

void copy_rect(struct image *src,
               struct image *dst,
               int src_x, int src_y,
               int dst_x, int dst_y,
               int w, int h);

void copy_rect_colorkey(struct image *src,
                struct image *dst,
                int src_x, int src_y,
                int dst_x, int dst_y,
                int w, int h,
                struct color_rgba colorkey);

void copy_rect_colorkey_scaled(struct image *src,
                               struct image *dst,
                               int src_x, int src_y,
                               int dst_x, int dst_y,
                               int src_w, int src_h,
                               int dst_w, int dst_h,
                               struct color_rgba colorkey); 

rect_t merge_rects(rect_t a, rect_t b);

/* Blitting */
bool image_blit(const struct image *src, struct image *dst, int dst_x, int dst_y);
bool image_blit_framebuffer(const struct image *src, struct framebuffer *dst, int dst_x, int dst_y);

bool image_blit_colorkey(const struct image *src, struct image *dst, int dst_x, int dst_y, struct color_rgba key);
bool image_blit_colorkey_clipped(const struct image *src, struct image *dst, rect_t clip,int dst_x, int dst_y, struct color_rgba key);
bool image_blit_scaled_nearest(const struct image *src, struct image *dst, int dst_x, int dst_y, int dst_w, int dst_h);
bool image_blit_scaled_nearest_keyed_clipped(const struct image *src, struct image *dst, rect_t clip, int dx, int dy, int dw, int dh, struct color_rgba key);
bool image_scale_nearest(const struct image *src, struct image *dst);
bool image_blit_clipped( struct image *src, struct image *dst, rect_t clip, int dst_x, int dst_y);

bool image_blit_scaled_nearest_keyed(const struct image *src, struct image *dst, int dx, int dy, int dw, int dh, struct color_rgba key);

#endif