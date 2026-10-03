#include "psf.h"

#include "video.h"
#include "framebuffer.h"
#include "serial.h"
#include "string.h"
#include "vfs.h"
#include "vmm.h"

// PC screen font

int load_psf1_font(psf_font_t *font, const char *path) {
    
    void *data;
    size_t size;

    serial_print("font path: %s\n",path);
    struct file *file = vfs_open(path, 0);
    if (!file || !file->inode) {
        serial_print("font file could not be opened\n");
        return 0;
    }

    data = kmalloc(file->inode->size);
    if (!data) {
        serial_print("font buffer allocation failed\n");
        vfs_close(file);
        return 0;
    }

    size = vfs_read(file, data, file->inode->size);

    if (size < sizeof(psf1_header_t)) {
        serial_print("psf size less then expected (siz: %d exp: %d)\n", size, sizeof(psf1_header_t));
        vfs_close(file);
        return 0;
    }

    psf1_header_t *hdr = (psf1_header_t *)data;

    if (hdr->magic[0] != PSF1_MAGIC0 || hdr->magic[1] != PSF1_MAGIC1) {
        serial_print("invalid font header");
        vfs_close(file);
        return 0;
    }

    uint32_t glyphs = (hdr->mode & 0x01) ? 512 : 256;
    uint32_t bytes_per_glyph = hdr->charsize;
    uint32_t needed = sizeof(psf1_header_t) + glyphs * bytes_per_glyph;

    if (size < needed) {
        serial_print("size too small for font");
        vfs_close(file);
        return 0;
    }

    font->width = 8;
    serial_print("hdr magic: %08x charsize: %d mode: %d\n",hdr->magic, hdr->charsize, hdr->mode);
    font->height = hdr->charsize;
    font->num_glyphs = glyphs;
    font->glyphs = (uint8_t *)data + sizeof(psf1_header_t);

    vfs_close(file);

    return 1;
}

void draw_psf1_char(struct image img, psf_font_t *font, int x, int y, uint8_t c, struct color_rgba color)
{

    if (c >= font->num_glyphs) {
        c = '?';
    }

    uint8_t *glyph = font->glyphs + c * font->height;

    for (uint32_t row = 0; row < font->height; row++) {
        uint8_t bits = glyph[row];

        for (uint32_t col = 0; col < 8; col++) {
            if (bits & (0x80 >> col)) {
                
               image_put_pixel(&img, col + x, row + y, color);
            }
        }
    }
}