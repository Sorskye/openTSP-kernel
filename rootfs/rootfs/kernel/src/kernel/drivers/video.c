#include "types.h"
#include "video.h"
#include "serial.h"
#include "string.h"


/* ----------------------------------------------------------
 * Helpers
 * ---------------------------------------------------------- */

static inline uint32_t bytes_per_pixel(const struct image *img)
{
    return img->fmt.bpp / 8;
}

static inline bool fmt_equal(const struct pixel_format *a, const struct pixel_format *b)
{
    return a->bpp        == b->bpp &&
           a->red_pos    == b->red_pos &&
           a->red_bits   == b->red_bits &&
           a->green_pos  == b->green_pos &&
           a->green_bits == b->green_bits &&
           a->blue_pos   == b->blue_pos &&
           a->blue_bits  == b->blue_bits;
}

static inline uint32_t extract_component(uint32_t pixel, uint8_t pos, uint8_t bits)
{
    if (bits == 0) return 0;
    uint32_t mask = (1u << bits) - 1u;
    return (pixel >> pos) & mask;
}

static inline uint32_t expand_to_8bit(uint32_t v, uint8_t bits)
{
    if (bits == 0) return 0;
    if (bits >= 8) return v & 0xFFu;
    return (v * 255u) / ((1u << bits) - 1u);
}

static inline uint32_t compress_from_8bit(uint32_t v, uint8_t bits)
{
    if (bits == 0) return 0;
    if (bits >= 8) return v & 0xFFu;
    return (v * ((1u << bits) - 1u)) / 255u;
}

static inline uint32_t pack_color_fmt(const struct pixel_format *fmt, uint8_t r, uint8_t g, uint8_t b)
{
    uint32_t r_max = (1u << fmt->red_bits) - 1u;
    uint32_t g_max = (1u << fmt->green_bits) - 1u;
    uint32_t b_max = (1u << fmt->blue_bits) - 1u;

    uint32_t rp = ((uint32_t)r * r_max) / 255u;
    uint32_t gp = ((uint32_t)g * g_max) / 255u;
    uint32_t bp = ((uint32_t)b * b_max) / 255u;

    return (rp << fmt->red_pos) |
           (gp << fmt->green_pos) |
           (bp << fmt->blue_pos);
}

static inline uint32_t convert_pixel_fmt(uint32_t src_pixel,
                                         const struct pixel_format *src,
                                         const struct pixel_format *dst)
{
    uint32_t sr = extract_component(src_pixel, src->red_pos,   src->red_bits);
    uint32_t sg = extract_component(src_pixel, src->green_pos, src->green_bits);
    uint32_t sb = extract_component(src_pixel, src->blue_pos,  src->blue_bits);

    sr = expand_to_8bit(sr, src->red_bits);
    sg = expand_to_8bit(sg, src->green_bits);
    sb = expand_to_8bit(sb, src->blue_bits);

    uint32_t dr = compress_from_8bit(sr, dst->red_bits);
    uint32_t dg = compress_from_8bit(sg, dst->green_bits);
    uint32_t db = compress_from_8bit(sb, dst->blue_bits);

    return (dr << dst->red_pos) |
           (dg << dst->green_pos) |
           (db << dst->blue_pos);
}

static inline uint8_t *pixel_ptr(const struct image *img, uint32_t x, uint32_t y)
{
    return (uint8_t *)img->addr + y * img->pitch + x * bytes_per_pixel(img);
}

static inline uint32_t image_read_pixel(const struct image *img, uint32_t x, uint32_t y)
{
    uint8_t *p = pixel_ptr(img, x, y);

    switch (img->fmt.bpp) {
        case 32:
            return *(uint32_t *)p;
        case 24:
            return ((uint32_t)p[0]) |
                   ((uint32_t)p[1] << 8) |
                   ((uint32_t)p[2] << 16);
        case 16:
        case 15:
            return *(uint16_t *)p;
        case 8:
            return *p;
        default:
            return 0;
    }
}

static inline void image_write_pixel_raw(const struct image *img, uint32_t x, uint32_t y, uint32_t pixel)
{
    uint8_t *p = pixel_ptr(img, x, y);

    switch (img->fmt.bpp) {
        case 32:
            *(uint32_t *)p = pixel;
            break;
        case 24:
            p[0] = (uint8_t)(pixel & 0xFF);
            p[1] = (uint8_t)((pixel >> 8) & 0xFF);
            p[2] = (uint8_t)((pixel >> 16) & 0xFF);
            break;
        case 16:
        case 15:
            *(uint16_t *)p = (uint16_t)pixel;
            break;
        case 8:
            *p = (uint8_t)pixel;
            break;
        default:
            break;
    }
}

/* ----------------------------------------------------------
 * Color / pixel API
 * ---------------------------------------------------------- */

struct color_rgba image_get_pixel(const struct image *img, uint32_t x, uint32_t y)
{
    struct color_rgba out = {0, 0, 0, 0};

    if (!img || !img->addr)
        return out;

    if (x >= img->width || y >= img->height)
        return out;

    const uint8_t *row = (const uint8_t *)img->addr + (size_t)y * img->pitch;
    const uint8_t *p   = row + (size_t)x * ((img->fmt.bpp + 7) / 8);

    uint32_t pixel = 0;

    switch (img->fmt.bpp) {
    case 8:
        pixel = p[0];
        break;
    case 16:
        pixel = (uint32_t)p[0]
              | ((uint32_t)p[1] << 8);
        break;
    case 24:
        pixel = (uint32_t)p[0]
              | ((uint32_t)p[1] << 8)
              | ((uint32_t)p[2] << 16);
        break;
    case 32:
        pixel = (uint32_t)p[0]
              | ((uint32_t)p[1] << 8)
              | ((uint32_t)p[2] << 16)
              | ((uint32_t)p[3] << 24);
        break;
    default:
        return out;
    }

    uint32_t r = 0, g = 0, b = 0;

    if (img->fmt.red_bits) {
        uint32_t mask = (1u << img->fmt.red_bits) - 1u;
        r = (pixel >> img->fmt.red_pos) & mask;
        r = (r * 255u) / mask;
    }

    if (img->fmt.green_bits) {
        uint32_t mask = (1u << img->fmt.green_bits) - 1u;
        g = (pixel >> img->fmt.green_pos) & mask;
        g = (g * 255u) / mask;
    }

    if (img->fmt.blue_bits) {
        uint32_t mask = (1u << img->fmt.blue_bits) - 1u;
        b = (pixel >> img->fmt.blue_pos) & mask;
        b = (b * 255u) / mask;
    }

    out.r = (uint8_t)r;
    out.g = (uint8_t)g;
    out.b = (uint8_t)b;
    out.a = 255;

    return out;
}

uint32_t image_make_color(const struct image *img, uint8_t r, uint8_t g, uint8_t b)
{
    if (!img) return 0;
    return pack_color_fmt(&img->fmt, r, g, b);
}

void image_put_pixel(struct image *img, uint32_t x, uint32_t y, struct color_rgba color)
{
    if (!img || !img->addr) return;
    if (x >= img->width || y >= img->height) return;

    uint32_t pixel = image_make_color(img, color.r, color.g, color.b);
    image_write_pixel_raw(img, x, y, pixel);
}

void image_clear(struct image *img, struct color_rgba color)
{
    if (!img || !img->addr) return;

    uint32_t pixel = image_make_color(img, color.r, color.g, color.b);

    if (img->fmt.bpp == 32) {
        for (uint32_t y = 0; y < img->height; y++) {
            uint32_t *row = (uint32_t *)((uint8_t *)img->addr + y * img->pitch);
            for (uint32_t x = 0; x < img->width; x++) {
                row[x] = pixel;
            }
        }
        return;
    }

    for (uint32_t y = 0; y < img->height; y++) {
        for (uint32_t x = 0; x < img->width; x++) {
            image_write_pixel_raw(img, x, y, pixel);
        }
    }
}

static bool color_equal(struct color_rgba a, struct color_rgba b)
{
    return a.r == b.r &&
           a.g == b.g &&
           a.b == b.b &&
           a.a == b.a;
}

// rect helpers
bool rect_intersect(rect_t a, rect_t b, rect_t *out)
{
    // Guard against degenerate rects — catch bad input early
    if (a.w <= 0 || a.h <= 0 || b.w <= 0 || b.h <= 0)
        return false;

    int x1 = (a.x > b.x) ? a.x : b.x;
    int y1 = (a.y > b.y) ? a.y : b.y;
    int x2 = ((a.x + a.w) < (b.x + b.w)) ? (a.x + a.w) : (b.x + b.w);
    int y2 = ((a.y + a.h) < (b.y + b.h)) ? (a.y + a.h) : (b.y + b.h);

    // Use < instead of <= if you want touching edges to count
    if (x2 <= x1 || y2 <= y1)
        return false;

    if (out) {
        out->x = x1;
        out->y = y1;
        out->w = x2 - x1;
        out->h = y2 - y1;
    }
    return true;
}

rect_t merge_rects(rect_t a, rect_t b) {
    rect_t r;
    r.x = (a.x < b.x) ? a.x : b.x;
    r.y = (a.y < b.y) ? a.y : b.y;
    int ax2 = a.x + a.w, bx2 = b.x + b.w;
    int ay2 = a.y + a.h, by2 = b.y + b.h;
    r.w = ((ax2 > bx2) ? ax2 : bx2) - r.x;
    r.h = ((ay2 > by2) ? ay2 : by2) - r.y;
    return r;
}

int rect_subtract(rect_t src, rect_t cut, rect_t out[4])
{
    rect_t inter;
    int count = 0;

    // If no overlap, the result is just the original rectangle
    if (!rect_intersect(src, cut, &inter)) {
        out[0] = src;
        return 1;
    }

    // Top
    if (inter.y > src.y) {
        out[count++] = (rect_t){
            src.x,
            src.y,
            src.w,
            inter.y - src.y
        };
    }

    // Bottom
    if (inter.y + inter.h < src.y + src.h) {
        out[count++] = (rect_t){
            src.x,
            inter.y + inter.h,
            src.w,
            (src.y + src.h) - (inter.y + inter.h)
        };
    }

    // Left
    if (inter.x > src.x) {
        out[count++] = (rect_t){
            src.x,
            inter.y,
            inter.x - src.x,
            inter.h
        };
    }

    // Right
    if (inter.x + inter.w < src.x + src.w) {
        out[count++] = (rect_t){
            inter.x + inter.w,
            inter.y,
            (src.x + src.w) - (inter.x + inter.w),
            inter.h
        };
    }

    return count;
}

void copy_rect(struct image *src,
               struct image *dst,
               int src_x, int src_y,
               int dst_x, int dst_y,
               int w, int h)
{
    if (!src || !dst) return;

    // Clip against src bounds
    if (src_x < 0) { w += src_x; dst_x -= src_x; src_x = 0; }
    if (src_y < 0) { h += src_y; dst_y -= src_y; src_y = 0; }
    if (src_x + w > (int)src->width) w = src->width - src_x;
    if (src_y + h > (int)src->height) h = src->height - src_y;

    // Clip against dst bounds
    if (dst_x < 0) { w += dst_x; src_x -= dst_x; dst_x = 0; }
    if (dst_y < 0) { h += dst_y; src_y -= dst_y; dst_y = 0; }
    if (dst_x + w > (int)dst->width) w = dst->width - dst_x;
    if (dst_y + h > (int)dst->height) h = dst->height - dst_y;

    if (w <= 0 || h <= 0) return;

    for (int iy = 0; iy < h; iy++) {
        for (int ix = 0; ix < w; ix++) {
            struct color_rgba c = image_get_pixel(src, src_x + ix, src_y + iy);
            image_put_pixel(dst, dst_x + ix, dst_y + iy, c);
        }
    }
}

void copy_rect_colorkey(struct image *src,
                struct image *dst,
                int src_x, int src_y,
                int dst_x, int dst_y,
                int w, int h,
                struct color_rgba colorkey)
{
    if (!src || !dst) return;

    // Clip against src bounds
    if (src_x < 0) { w += src_x; dst_x -= src_x; src_x = 0; }
    if (src_y < 0) { h += src_y; dst_y -= src_y; src_y = 0; }
    if (src_x + w > (int)src->width) w = src->width - src_x;
    if (src_y + h > (int)src->height) h = src->height - src_y;

    // Clip against dst bounds
    if (dst_x < 0) { w += dst_x; src_x -= dst_x; dst_x = 0; }
    if (dst_y < 0) { h += dst_y; src_y -= dst_y; dst_y = 0; }
    if (dst_x + w > (int)dst->width) w = dst->width - dst_x;
    if (dst_y + h > (int)dst->height) h = dst->height - dst_y;

    if (w <= 0 || h <= 0) return;

    for (int iy = 0; iy < h; iy++) {
        for (int ix = 0; ix < w; ix++) {
            struct color_rgba c = image_get_pixel(src, src_x + ix, src_y + iy);
            if (!color_equal(c, colorkey)){
                  image_put_pixel(dst, dst_x + ix, dst_y + iy, c);
            }
        }
    }
}

void copy_rect_colorkey_scaled(struct image *src,
                               struct image *dst,
                               int src_x, int src_y,
                               int dst_x, int dst_y,
                               int src_w, int src_h,
                               int dst_w, int dst_h,
                               struct color_rgba colorkey)
{
    if (!src || !dst) return;

    // Clip src region against src bounds
    if (src_x < 0) { src_w += src_x; src_x = 0; }
    if (src_y < 0) { src_h += src_y; src_y = 0; }
    if (src_x + src_w > (int)src->width)  src_w = src->width  - src_x;
    if (src_y + src_h > (int)src->height) src_h = src->height - src_y;

    // Clip dst region against dst bounds
    int clip_x = 0, clip_y = 0;
    if (dst_x < 0) { clip_x = -dst_x; dst_w += dst_x; dst_x = 0; }
    if (dst_y < 0) { clip_y = -dst_y; dst_h += dst_y; dst_y = 0; }
    if (dst_x + dst_w > (int)dst->width)  dst_w = dst->width  - dst_x;
    if (dst_y + dst_h > (int)dst->height) dst_h = dst->height - dst_y;

    if (src_w <= 0 || src_h <= 0) return;
    if (dst_w <= 0 || dst_h <= 0) return;

    for (int iy = 0; iy < dst_h; iy++) {
        // Map destination y back to source y (nearest neighbor)
        int sy = src_y + ((iy + clip_y) * src_h) / (dst_h + clip_y);

        for (int ix = 0; ix < dst_w; ix++) {
            // Map destination x back to source x (nearest neighbor)
            int sx = src_x + ((ix + clip_x) * src_w) / (dst_w + clip_x);

            struct color_rgba c = image_get_pixel(src, sx, sy);
            if (!color_equal(c, colorkey)) {
                image_put_pixel(dst, dst_x + ix, dst_y + iy, c);
            }
        }
    }
}


/* ----------------------------------------------------------
 * Image creation
 * ---------------------------------------------------------- */

struct image *image_create(uint32_t width, uint32_t height, const struct pixel_format *fmt)
{
    if (!fmt) return NULL;
    if (width == 0 || height == 0) return NULL;
    if (fmt->bpp == 0 || (fmt->bpp % 8) != 0) return NULL;

    struct image *img = kmalloc(sizeof(struct image));
    if (!img) return NULL;

    uint32_t bpp_bytes = fmt->bpp / 8;
    img->width = width;
    img->height = height;
    img->pitch = width * bpp_bytes;
    img->fmt = *fmt;
    img->owns_memory = true;

    uint32_t size = img->pitch * img->height;
    img->addr = kmalloc(size);

    if (!img->addr) {
        kfree(img);
        return NULL;
    }

    memset(img->addr, 0, size);
    return img;
}

struct image image_wrap(void *addr,
                        uint32_t width,
                        uint32_t height,
                        uint32_t pitch,
                        const struct pixel_format *fmt)
{
    struct image img;
    img.addr = addr;
    img.width = width;
    img.height = height;
    img.pitch = pitch;
    img.fmt = *fmt;
    img.owns_memory = false;
    return img;
}

struct image *image_clone(const struct image *src)
{
    if (!src || !src->addr) return NULL;

    struct image *dst = image_create(src->width, src->height, &src->fmt);
    if (!dst) return NULL;

    memcpy(dst->addr, src->addr, src->pitch * src->height);
    return dst;
}

void image_destroy(struct image *img)
{
    if (!img) return;

    if (img->owns_memory && img->addr) {
        kfree(img->addr);
    }

    kfree(img);
}


/* ----------------------------------------------------------
 * Generic blit internals
 * ---------------------------------------------------------- */

static bool image_blit_internal(const struct image *src,
                                struct image *dst,
                                int dst_x,
                                int dst_y,
                                const struct blit_opts *opts)
{
    if (!src || !dst || !src->addr || !dst->addr) return false;
    if (src->width == 0 || src->height == 0) return false;

    bool use_colorkey = (opts && (opts->flags & BLIT_COLORKEY)) ? true : false;
    bool same_format = fmt_equal(&src->fmt, &dst->fmt);

    uint32_t src_x0 = 0;
    uint32_t src_y0 = 0;
    uint32_t copy_w = src->width;
    uint32_t copy_h = src->height;

    if (dst_x < 0) {
        uint32_t skip = (uint32_t)(-dst_x);
        if (skip >= copy_w) return false;
        src_x0 += skip;
        copy_w -= skip;
        dst_x = 0;
    }

    if (dst_y < 0) {
        uint32_t skip = (uint32_t)(-dst_y);
        if (skip >= copy_h) return false;
        src_y0 += skip;
        copy_h -= skip;
        dst_y = 0;
    }

    if ((uint32_t)dst_x >= dst->width || (uint32_t)dst_y >= dst->height) {
        return false;
    }

    if ((uint32_t)dst_x + copy_w > dst->width) {
        copy_w = dst->width - (uint32_t)dst_x;
    }

    if ((uint32_t)dst_y + copy_h > dst->height) {
        copy_h = dst->height - (uint32_t)dst_y;
    }

    if (copy_w == 0 || copy_h == 0) return false;

    uint32_t key_src = 0;
    if (use_colorkey) {
        key_src = pack_color_fmt(&src->fmt,
                                 opts->colorkey.r,
                                 opts->colorkey.g,
                                 opts->colorkey.b);
    }

    if (same_format && !use_colorkey && src->fmt.bpp == 32) {
        for (uint32_t row = 0; row < copy_h; row++) {
            uint8_t *s = (uint8_t *)src->addr + (src_y0 + row) * src->pitch + src_x0 * 4;
            uint8_t *d = (uint8_t *)dst->addr + ((uint32_t)dst_y + row) * dst->pitch + (uint32_t)dst_x * 4;
            memcpy(d, s, copy_w * 4);
        }
        return true;
    }

    for (uint32_t row = 0; row < copy_h; row++) {
        for (uint32_t col = 0; col < copy_w; col++) {
            uint32_t sp = image_read_pixel(src, src_x0 + col, src_y0 + row);

            if (use_colorkey && sp == key_src) {
                continue;
            }

            uint32_t dp = same_format ? sp : convert_pixel_fmt(sp, &src->fmt, &dst->fmt);
            image_write_pixel_raw(dst, (uint32_t)dst_x + col, (uint32_t)dst_y + row, dp);
        }
    }

    return true;
}



/* ----------------------------------------------------------
 * Public blit API
 * ---------------------------------------------------------- */

bool image_blit(const struct image *src, struct image *dst, int dst_x, int dst_y)
{
    return image_blit_internal(src, dst, dst_x, dst_y, NULL);
}

bool image_blit_clipped(
    struct image *src,
    struct image *dst,
    rect_t clip,
    int dst_x,
    int dst_y)
{
    if (!src || !dst)
        return false;

    // Destination rectangle of the blit
    rect_t blit = { dst_x, dst_y, src->width, src->height };
    rect_t inter;

    // If nothing intersects, nothing to draw
    if (!rect_intersect(blit, clip, &inter))
        return true;

    // Compute starting offsets inside the source image
    int start_sx = inter.x - dst_x;
    int start_sy = inter.y - dst_y;

    for (int y = 0; y < inter.h; y++) {
        int sy = start_sy + y;

        for (int x = 0; x < inter.w; x++) {
            int sx = start_sx + x;

            struct color_rgba c = image_get_pixel(src, sx, sy);
            image_put_pixel(dst, inter.x + x, inter.y + y, c);
        }
    }

    return true;
}


bool image_blit_colorkey(const struct image *src,
                         struct image *dst,
                         int dst_x,
                         int dst_y,
                         struct color_rgba key)
{
    if (!src || !dst)
        return false;

    if (!src->addr || !dst->addr)
        return false;

    for (uint32_t sy = 0; sy < src->height; sy++) {
        for (uint32_t sx = 0; sx < src->width; sx++) {
            struct color_rgba c = image_get_pixel(src, sx, sy);

            if (color_equal(c, key))
                continue;

            image_put_pixel(dst, dst_x + (int)sx, dst_y + (int)sy, c);
        }
    }

    return true;
}

bool image_blit_colorkey_clipped(
    const struct image *src,
    struct image *dst,
    rect_t clip,
    int dst_x,
    int dst_y,
    struct color_rgba key)
{
    if (!src || !dst)
        return false;

    // Compute the blit rectangle
    rect_t blit = { dst_x, dst_y, src->width, src->height };
    rect_t inter;

    // If no intersection, nothing to draw
    if (!rect_intersect(blit, clip, &inter))
        return true;

    // Compute starting offsets inside the source image
    int start_sx = inter.x - dst_x;
    int start_sy = inter.y - dst_y;

    for (int y = 0; y < inter.h; y++) {
        int sy = start_sy + y;

        for (int x = 0; x < inter.w; x++) {
            int sx = start_sx + x;

            struct color_rgba c = image_get_pixel(src, sx, sy);
            if (color_equal(c, key))
                continue;

            image_put_pixel(dst, inter.x + x, inter.y + y, c);
        }
    }

    return true;
}


bool image_blit_scaled_nearest_keyed_clipped(const struct image *src, struct image *dst, rect_t clip, int dx, int dy, int dw, int dh, struct color_rgba key)
{
    if (!src || !dst)
        return false;

    if (dw <= 0 || dh <= 0)
        return false;

    // Destination rectangle
    rect_t blit = { dx, dy, dw, dh };
    rect_t inter;

    // If nothing intersects, nothing to draw
    if (!rect_intersect(blit, clip, &inter))
        return true;

    // Compute starting offsets inside the scaled area
    int start_x = inter.x - dx;
    int start_y = inter.y - dy;

    for (int y = 0; y < inter.h; y++) {
        int sy = (start_y + y) * src->height / dh;

        for (int x = 0; x < inter.w; x++) {
            int sx = (start_x + x) * src->width / dw;

            struct color_rgba c = image_get_pixel(src, sx, sy);
            if (color_equal(c, key))
                continue;

            image_put_pixel(dst, inter.x + x, inter.y + y, c);
        }
    }

    return true;
}


bool image_blit_scaled_nearest(const struct image *src,
                               struct image *dst,
                               int dst_x,
                               int dst_y,
                               int dst_w,
                               int dst_h)
{
    if (!src || !dst || !src->addr || !dst->addr) return false;
    if (src->width == 0 || src->height == 0) return false;
    if (dst_w <= 0 || dst_h <= 0) return false;

    bool same_format = fmt_equal(&src->fmt, &dst->fmt);

    for (int y = 0; y < dst_h; y++) {
        int sy = (y * (int)src->height) / dst_h;
        int dy = dst_y + y;

        if (dy < 0 || dy >= (int)dst->height) continue;

        for (int x = 0; x < dst_w; x++) {
            int sx = (x * (int)src->width) / dst_w;
            int dx = dst_x + x;

            if (dx < 0 || dx >= (int)dst->width) continue;

            uint32_t sp = image_read_pixel(src, (uint32_t)sx, (uint32_t)sy);
            uint32_t dp = same_format ? sp : convert_pixel_fmt(sp, &src->fmt, &dst->fmt);
            image_write_pixel_raw(dst, (uint32_t)dx, (uint32_t)dy, dp);
        }
    }

    return true;
}

bool image_scale_nearest(const struct image *src, struct image *dst)
{
    if (!src || !dst) return false;
    return image_blit_scaled_nearest(src, dst, 0, 0, (int)dst->width, (int)dst->height);
}



bool image_blit_scaled_nearest_keyed(
    const struct image *src,
    struct image *dst,
    int dx,
    int dy,
    int dw,
    int dh,
    struct color_rgba key
)
{
    if (!src || !dst)
        return false;

    if (dw <= 0 || dh <= 0)
        return false;

    for (int y = 0; y < dh; y++) {
        int sy = (y * (int)src->height) / dh;

        for (int x = 0; x < dw; x++) {
            int sx = (x * (int)src->width) / dw;

            struct color_rgba c = image_get_pixel(src, sx, sy);
            

            if (color_equal(c, key))
                continue;

            image_put_pixel(dst, dx + x, dy + y, c);
        }
    }

    return true;
}