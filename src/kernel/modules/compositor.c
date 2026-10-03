#include "types.h"

#include "serial.h"
#include "sleep.h"
#include "task.h"
#include "tty.h"
#include "vmm.h"
#include "spinlock.h"
#include "string.h"
#include "ps2_mouse.h"
#include "pit.h"

#include "video.h"
#include "psf.h"
#include "compositor.h"
#include "bitmap.h"
#include "inputsink.h"



#define MAX_WINDOWS 255

spinlock_t frame_allowed_lock = {0};
static bool frame_allowed = true;

window_t window_list[MAX_WINDOWS];
static uint8_t window_index = 0;

/* Hardware framebuffer is now just an image view */
static struct image hardware_buffer = {0};

/* Offscreen compositor output */
static struct image *buffer_out = NULL;

/* Shared pixel format used for created images */
static struct pixel_format g_pixfmt = {0};

static window_border_theme_t g_theme = {0};

// drag state
static struct {
    window_t *win;
    int grab_x;
    int grab_y;
    int release_counter;
    bool active;
} g_drag = {0};

// graphical mouse
int16_t mouse_pos_x = 10;
int16_t mouse_pos_y = 10;

int16_t mouse_min_x = 0;
int16_t mouse_min_y = 0;
int16_t mouse_max_x = 100;
int16_t mouse_max_y = 100;
static uint8_t current_buttons = 0;  // tracks held buttons across events

// console
int SCROLL_X0 = 0;
    int SCROLL_X1 = 0;
int SCROLL_Y0 = 0;
    int SCROLL_Y1 = 0;

    int cursor_x = 0;
    int cursor_y = 0;
    struct image *background = {0};


// damage
#define MAX_DAMAGE 128




static struct image* load_theme_bmp(const char* path)
{
    return bmp_load_image_alloc(path, &g_pixfmt);
}

static bool load_window_theme(void)
{
    g_theme.tl = load_theme_bmp("sys/dat/i386-theme/wdborder/TL.bmp");
    g_theme.t  = load_theme_bmp("sys/dat/i386-theme/wdborder/T.bmp");
    g_theme.tr = load_theme_bmp("sys/dat/i386-theme/wdborder/TR.bmp");
    g_theme.l  = load_theme_bmp("sys/dat/i386-theme/wdborder/L.bmp");
    g_theme.r  = load_theme_bmp("sys/dat/i386-theme/wdborder/R.bmp");
    g_theme.bl = load_theme_bmp("sys/dat/i386-theme/wdborder/BL.bmp");
    g_theme.b  = load_theme_bmp("sys/dat/i386-theme/wdborder/B.bmp");
    g_theme.br = load_theme_bmp("sys/dat/i386-theme/wdborder/BR.bmp");

    
    int loadedfont = load_psf1_font(&g_theme.font, "/sys/fonts/default8x16.psf");
    serial_print("%d\n",loadedfont);

    g_theme.corner_w = 27;
    g_theme.corner_h = 27;

    return g_theme.tl && g_theme.t && g_theme.tr &&
           g_theme.l  && g_theme.r &&
           g_theme.bl && g_theme.b && g_theme.br;
}




void compositor_init(void)
{
    struct framebuffer fb = get_framebuffer();
    serial_print("framebuffer at 0x%x\n", fb.addr);
    mouse_max_y = (uint16_t)fb.height;
    mouse_max_x = (uint16_t)fb.width;

    g_pixfmt.bpp = fb.fmt.bpp;
    g_pixfmt.red_pos = fb.fmt.red_pos;
    g_pixfmt.red_bits = fb.fmt.red_bits;
    g_pixfmt.green_pos = fb.fmt.green_pos;
    g_pixfmt.green_bits = fb.fmt.green_bits;
    g_pixfmt.blue_pos = fb.fmt.blue_pos;
    g_pixfmt.blue_bits = fb.fmt.blue_bits;

    hardware_buffer = image_wrap(
        fb.addr,
        fb.width,
        fb.height,
        fb.pitch,
        &g_pixfmt
    );

    buffer_out = image_create(hardware_buffer.width, hardware_buffer.height, &g_pixfmt);
    if (!buffer_out) {
        serial_print("ERR: failed to allocate compositor output buffer\n");
        return;
    }
        SCROLL_X1 = buffer_out->width;
    SCROLL_Y1 = buffer_out->height;

    load_window_theme();

  
}




char read_tty_output(tty_t* tty)
{   
    while (tty->output_head == tty->output_tail) {
        tty->task_backend_wait = current_task;
        block_task(current_task);
        if (tty->output_head != tty->output_tail) {
            wake_task(current_task);
        }
        if (current_task->state == TASK_BLOCKED) {
            __asm__ __volatile__("int $32");
        }
    }
    if (tty->task_backend_wait == current_task) {
        tty->task_backend_wait = NULL;
    }

    spinlock_acquire(&tty->output_lock);
    char c = tty->output_buff[tty->output_tail];
    tty->output_tail = (tty->output_tail + 1) % TTY_OUTPUT_BUFF_SIZE;
    spinlock_release(&tty->output_lock);

    return c;
}

void compositor_clear_screen(){
    rect_t d = {
        .x = 0,
        .y = 0,
        .w = buffer_out->width,
        .h = buffer_out->height
    };

   
    copy_rect(background, &hardware_buffer, d.x,d.y,d.x,d.y,d.w,d.h);

    cursor_x = 0;
    cursor_y = 0;
}

void compositor_main(void)
{
    compositor_init();

    struct color_rgba colorkey = {255,0,255,255};
    struct color_rgba bg = {30, 30, 30};
    // background
    struct image *img_raw = bmp_load_image_alloc("/sys/dat/i386-theme/wallpaper.bmp", &g_pixfmt);
    if (!img_raw) {
        serial_print("ERR: failed to load wallpaper\n");
    }


    background = image_create(hardware_buffer.width, hardware_buffer.height, &g_pixfmt);
    if (!background) {
        serial_print("ERR: failed to create scaled wallpaper image\n");
        image_destroy(background);
       // return;
    }

    
    image_scale_nearest(img_raw, background);

    rect_t d = {
        .x = 0,
        .y = 0,
        .w = buffer_out->width,
        .h = buffer_out->height
    };

   // copy_rect(background, buffer_out, d.x,d.y,d.x,d.y,d.w,d.h);
    copy_rect(buffer_out, &hardware_buffer, d.x,d.y,d.x,d.y,d.w,d.h);
   

    psf_font_t *font = &g_theme.font;
    
    struct color_rgba textcolor = {255,255,255,255};
    struct color_rgba black = {0,0,0,255};
    
    serial_print("lets get started\n");

    compositor_clear_screen();
    while (1) {
        char c = read_tty_output(get_active_tty());

        rect_t r = {
            .w = font->width,
            .h = font->height
        };
        int needs_flush = 0;

        if (c == '\b') {
            if (cursor_x > SCROLL_X0) {
                cursor_x -= font->width;
                r.x = cursor_x;
                r.y = cursor_y;
                copy_rect(background, buffer_out, r.x,r.y,r.x,r.y,r.w,r.h);
                draw_psf1_char(*buffer_out, font, cursor_x, cursor_y, ' ', textcolor);
                needs_flush = 1;
                
            } else if (cursor_y > SCROLL_Y0) {
                cursor_y-= font->height;
                cursor_x = SCROLL_X1;
                r.x = cursor_x;
                r.y = cursor_y;
                copy_rect(background, buffer_out, r.x,r.y,r.x,r.y,r.w,r.h);
                draw_psf1_char(*buffer_out, font, cursor_x, cursor_y, ' ', textcolor);
                needs_flush = 1;
            }
        } else if (c == '\n' || cursor_x >= SCROLL_X1) {
            cursor_x = SCROLL_X0;
            if (cursor_y < SCROLL_Y1) {
                cursor_y += font->height;
            } else {
               // compositor_scroll();
                cursor_y = SCROLL_Y1;
            }
        } else {
            r.x = cursor_x;
            r.y = cursor_y;

            copy_rect(background, buffer_out, r.x,r.y,r.x,r.y,r.w,r.h);
            draw_psf1_char(*buffer_out, font, cursor_x, cursor_y, c, textcolor);
            cursor_x += font->width;
            needs_flush = 1;
        }

        if (needs_flush) {
            copy_rect_colorkey(buffer_out, &hardware_buffer, r.x, r.y, r.x, r.y, r.w, r.h, black);
        }

        // set cursor

        // 3. Flush to hardware
        

    }
}