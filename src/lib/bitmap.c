#include "types.h"
#include "stdio.h"
#include "string.h"
#include "serial.h"
#include "vmm.h"
#include "video.h"
#include "bitmap.h"
#include "vfs.h"

#include "syscall.h"

static uint16_t rd16(const uint8_t* p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd32(const uint8_t* p)
{
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

static int32_t rd32s(const uint8_t* p)
{
    return (int32_t)rd32(p);
}

static bool checked_range(size_t offset, size_t size, size_t total)
{
    if (offset > total) return false;
    if (size > total - offset) return false;
    return true;
}

static bool checked_mul_u32(uint32_t a, uint32_t b, uint32_t* out)
{
    if (!out) return false;

    if (a == 0 || b == 0) {
        *out = 0;
        return true;
    }

    if (a > UINT32_MAX / b)
        return false;

    *out = a * b;
    return true;
}

static bool checked_mul_size(size_t a, size_t b, size_t* out)
{
    if (!out) return false;

    if (a == 0 || b == 0) {
        *out = 0;
        return true;
    }

    if (a > ((size_t)-1) / b)
        return false;

    *out = a * b;
    return true;
}

static inline uint32_t pack_rgba32(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    return ((uint32_t)a << 24) |
           ((uint32_t)r << 16) |
           ((uint32_t)g << 8)  |
           ((uint32_t)b);
}

/*
 * Internal helper:
 * Decode BMP into a temporary 32-bit RGBA image.
 *
 * Output format:
 *   bpp = 32
 *   red_pos   = 16
 *   green_pos = 8
 *   blue_pos  = 0
 *
 * Alpha is loaded from 32-bit BMPs as-is.
 */
static struct image *bmp_decode_to_rgba32(const void *data, size_t size)
{
    const uint8_t *p = (const uint8_t *)data;

    if (!data) {
        serial_print("bmp: no input data\n");
        return NULL;
    }

    /* BITMAPFILEHEADER */
    if (size < 14) {
        serial_print("bmp: file too small for BITMAPFILEHEADER\n");
        return NULL;
    }

    /* Signature "BM" */
    if (p[0] != 'B' || p[1] != 'M') {
        serial_print("bmp: invalid signature\n");
        return NULL;
    }

    uint32_t bfSize    = rd32(p + 2);
    uint32_t bfOffBits = rd32(p + 10);

    if (bfSize != 0 && bfSize > size) {
        serial_print("bmp: claimed file size exceeds actual buffer\n");
        return NULL;
    }

    if (!checked_range(14, 4, size)) {
        serial_print("bmp: missing DIB header size\n");
        return NULL;
    }

    uint32_t dibSize = rd32(p + 14);

    if (dibSize < 40) {
        serial_print("bmp: unsupported DIB header size\n");
        return NULL;
    }

    if (!checked_range(14, dibSize, size)) {
        serial_print("bmp: DIB header exceeds file size\n");
        return NULL;
    }

    const uint8_t *dib = p + 14;

    int32_t  biWidth       = rd32s(dib + 4);
    int32_t  biHeight      = rd32s(dib + 8);
    uint16_t biPlanes      = rd16(dib + 12);
    uint16_t biBitCount    = rd16(dib + 14);
    uint32_t biCompression = rd32(dib + 16);
    uint32_t biSizeImage   = rd32(dib + 20);

    if (biPlanes != 1) {
        serial_print("bmp: biPlanes != 1\n");
        return NULL;
    }

    if (biWidth <= 0) {
        serial_print("bmp: invalid width\n");
        return NULL;
    }

    if (biHeight == 0) {
        serial_print("bmp: invalid height\n");
        return NULL;
    }

    bool top_down = false;
    uint32_t width = (uint32_t)biWidth;
    uint32_t height = 0;

    if (biHeight < 0) {
        top_down = true;

        if (biHeight == INT32_MIN) {
            serial_print("bmp: invalid negative height\n");
            return NULL;
        }

        height = (uint32_t)(-biHeight);
    } else {
        height = (uint32_t)biHeight;
    }

    /* Support only uncompressed 24/32-bit BMP for now */
    if (biCompression != 0) {
        serial_print("bmp: only BI_RGB uncompressed BMP supported\n");
        return NULL;
    }

    if (!(biBitCount == 24 || biBitCount == 32)) {
        serial_print("bmp: only 24-bit and 32-bit BMP supported\n");
        return NULL;
    }

    uint32_t bits_per_row;
    uint32_t row_stride;

    if (!checked_mul_u32(width, (uint32_t)biBitCount, &bits_per_row)) {
        serial_print("bmp: row bit count overflow\n");
        return NULL;
    }

    /* BMP rows padded to 4-byte boundary */
    uint32_t row_blocks = (bits_per_row + 31u) / 32u;

    if (!checked_mul_u32(row_blocks, 4u, &row_stride)) {
        serial_print("bmp: row stride overflow\n");
        return NULL;
    }

    size_t pixel_array_size;
    if (!checked_mul_size((size_t)row_stride, (size_t)height, &pixel_array_size)) {
        serial_print("bmp: pixel array size overflow\n");
        return NULL;
    }

    if (bfOffBits < 14 + dibSize) {
        serial_print("bmp: pixel array offset overlaps headers\n");
        return NULL;
    }

    if (!checked_range((size_t)bfOffBits, pixel_array_size, size)) {
        serial_print("bmp: pixel array exceeds file size\n");
        return NULL;
    }

    if (biSizeImage != 0 && biSizeImage < pixel_array_size) {
        serial_print("bmp: biSizeImage too small\n");
        return NULL;
    }

    struct pixel_format rgba32_fmt = {
        .bpp = 32,
        .red_pos = 16,   .red_bits = 8,
        .green_pos = 8,  .green_bits = 8,
        .blue_pos = 0,   .blue_bits = 8
    };

    struct image *img = image_create(width, height, &rgba32_fmt);
    if (!img) {
        serial_print("bmp: failed to allocate output image\n");
        return NULL;
    }

    const uint8_t *pixels = p + bfOffBits;

    for (uint32_t y = 0; y < height; y++) {
        uint32_t src_y = top_down ? y : (height - 1u - y);

        const uint8_t *src_row = pixels + (size_t)src_y * row_stride;
        uint32_t *dst_row = (uint32_t *)((uint8_t *)img->addr + (size_t)y * img->pitch);

        if (biBitCount == 24) {
            for (uint32_t x = 0; x < width; x++) {
                size_t px = (size_t)x * 3u;
                uint8_t b = src_row[px + 0];
                uint8_t g = src_row[px + 1];
                uint8_t r = src_row[px + 2];

                dst_row[x] = pack_rgba32(r, g, b, 255);
            }
        } else {
            for (uint32_t x = 0; x < width; x++) {
                size_t px = (size_t)x * 4u;
                uint8_t b = src_row[px + 0];
                uint8_t g = src_row[px + 1];
                uint8_t r = src_row[px + 2];
                uint8_t a = src_row[px + 3];

                /*
                 * For BI_RGB 32-bit BMP, alpha is often undefined.
                 * If you want opaque-only behavior, replace 'a' with 255.
                 */
                dst_row[x] = pack_rgba32(r, g, b, a);
            }
        }
    }

    return img;
}

bool bmp_load_image(const char *path, struct image *out)
{
    struct file* file;
    char *file_data;
    size_t file_size;
    struct image *decoded;

    if (!path || !out)
        return false;

    if (!out->addr) {
        serial_print("bmp: destination image has no backing memory\n");
        return false;
    }

    file = vfs_open(path,0);
    if (file < 0) {
        serial_print("bmp: fopen failed\n");
        return false;
    }

    uint32_t read_bytes = vfs_read(file, file_data ,file_size);
    vfs_close(file);

    serial_print("bmp: %s size=%d\n", path, file_size);

    if (!file_data) {
        serial_print("bmp: fread failed\n");
        return false;
    }

    decoded = bmp_decode_to_rgba32(file_data, file_size);
    kfree(file_data);

    if (!decoded)
        return false;

    if (decoded->width != out->width || decoded->height != out->height) {
        serial_print("bmp: destination image size mismatch\n");
        image_destroy(decoded);
        return false;
    }

    /*
     * If formats match, this is a direct blit.
     * Otherwise image_blit() will convert pixel format.
     */
    bool ok = image_blit(decoded, out, 0, 0);
    image_destroy(decoded);

    return ok;
}

struct image *bmp_load_image_alloc(const char *path, const struct pixel_format *fmt)
{
    struct file* file;
    char *file_data;
    size_t file_size;
    struct image *decoded;
    struct image *out;

    if (!path || !fmt)
        return NULL;

    file = vfs_open(path, O_RDWR);
    if (!file || !file->inode) {
        serial_print("bmp: fopen failed for %s\n", path);
        return NULL;
    }

    file_size = file->inode->size;
    serial_print("inode at: 0x%x\n", file->inode);

    file_data = kmalloc(file_size);
    size_t read_bytes = vfs_read(file, file_data, file_size);
    vfs_close(file);
   
    serial_print("bmp alloc: %s readbytes: %d size=%d data=0x%x\n", path,read_bytes ,file_size, file_data);

    if (!file_data) {
        serial_print("bmp: fread failed\n");
        return NULL;
    }

    decoded = bmp_decode_to_rgba32(file_data, file_size);
    kfree(file_data);

    if (!decoded)
        return NULL;

    out = image_create(decoded->width, decoded->height, fmt);
    if (!out) {
        image_destroy(decoded);
        return NULL;
    }

    if (!image_blit(decoded, out, 0, 0)) {
        image_destroy(decoded);
        image_destroy(out);
        return NULL;
    }

    image_destroy(decoded);
    return out;
}