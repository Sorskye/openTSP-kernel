#ifndef BITMAP_H
#define BITMAP_H

#include "types.h"
#include "video.h"
#include "framebuffer.h"

bool bmp_load_image(const char *path, struct image *out);
struct image *bmp_load_image_alloc(const char *path, const struct pixel_format *fmt);

#endif