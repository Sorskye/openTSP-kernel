#ifndef PSF_H
#define PSF_H

#include "types.h"
#include "framebuffer.h"
#include "video.h"

typedef struct {
    uint8_t magic[2];
    uint8_t mode;
    uint8_t charsize;
} __attribute__((packed)) psf1_header_t;

#define PSF1_MAGIC0 0x36
#define PSF1_MAGIC1 0x04

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t num_glyphs;
    uint8_t *glyphs;
} psf_font_t;

int load_psf1_font(psf_font_t *font, const char *path);

void draw_psf1_char(struct image img, psf_font_t *font, int x, int y, uint8_t c, struct color_rgba color);

#endif