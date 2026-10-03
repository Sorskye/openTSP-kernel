
#include "types.h"
#include "vmm.h"
#include "framebuffer.h"
#include "serial.h"
#include "lpcspeak.h"

struct framebuffer fb;

void framebuffer_init(struct multiboot_info* mbinfo)
{
    if(!(mbinfo->flags & (1 << 12))) {
        serial_print("FATAL: framebuffer info inavailable\n");
    }

    serial_print("init framebuffer\n");

    fb.addr = (uint32_t*)(uintptr_t)mbinfo->framebuffer_addr;
    fb.width = mbinfo->framebuffer_width;
    fb.height = mbinfo->framebuffer_height;
    fb.pitch = mbinfo->framebuffer_pitch;

    fb.fmt.bpp = (uint8_t)mbinfo->framebuffer_bpp;

    

    fb.fmt.red_pos = 16;//(uint8_t)mbinfo->framebuffer_red_field_position;
    fb.fmt.red_bits = 8; //(uint8_t)mbinfo->framebuffer_red_mask_size;
    fb.fmt.green_pos = 8; //(uint8_t)mbinfo->framebuffer_green_field_position;
    fb.fmt.green_bits =8;//(uint8_t) mbinfo->framebuffer_green_mask_size;
    fb.fmt.blue_pos = 0;//(uint8_t)mbinfo->framebuffer_blue_field_position;
    fb.fmt.blue_bits =8; //(uint8_t)mbinfo->framebuffer_blue_mask_size;

    serial_print("framebuffer w: %d, h: %d\n",fb.width, fb.height);
   
  
    serial_print("framebuffer initialized\n");
}


// always use: y * pitch + x * bytes_per_pixel
struct framebuffer get_framebuffer()
{
    return fb;
}

// returns reference_framebuffer on failure





uint32_t fb_make_color(struct framebuffer fb, uint8_t r, uint8_t g, uint8_t b)
{
    serial_print("fb make color");
    uint32_t r_max = (1u << fb.fmt.red_bits) - 1;
    uint32_t g_max = (1u << fb.fmt.green_bits) - 1;
    uint32_t b_max = (1u << fb.fmt.blue_bits) - 1;

    uint32_t rp = ((uint32_t)r * r_max) / 255;
    uint32_t gp = ((uint32_t)g * g_max) / 255;
    uint32_t bp = ((uint32_t)b * b_max) / 255;

    return (rp << fb.fmt.red_pos) |
           (gp << fb.fmt.green_pos) |
           (bp << fb.fmt.blue_pos);
}


