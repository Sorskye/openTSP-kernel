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
static uint32_t console_columns = 0;
static uint32_t console_rows = 0;
static uint8_t *console_cells = NULL;


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

static void compositor_redraw_text(psf_font_t *font)
{
    image_blit(background, buffer_out, 0, 0);

    for (uint32_t row = 0; row < console_rows; row++) {
        for (uint32_t column = 0; column < console_columns; column++) {
            uint8_t c = console_cells[row * console_columns + column];
            if (c != ' ') {
                draw_psf1_char(*buffer_out, font,
                               column * font->width, row * font->height,
                               c, (struct color_rgba){255, 255, 255, 255});
            }
        }
    }

    image_blit(buffer_out, &hardware_buffer, 0, 0);
}

static void compositor_scroll(psf_font_t *font)
{
    if (console_rows > 1) {
        memmove(console_cells,
                console_cells + console_columns,
                (console_rows - 1) * console_columns);
    }
    memset(console_cells + (console_rows - 1) * console_columns,
           ' ', console_columns);

    cursor_y = (console_rows - 1) * font->height;
    compositor_redraw_text(font);
}

static void compositor_advance_line(psf_font_t *font)
{
    cursor_x = SCROLL_X0;
    if ((uint32_t)cursor_y + font->height < (uint32_t)SCROLL_Y1) {
        cursor_y += font->height;
    } else {
        compositor_scroll(font);
    }
}

static void compositor_put_char(psf_font_t *font,
                                uint8_t c,
                                struct color_rgba textcolor)
{
    uint32_t column = cursor_x / font->width;
    uint32_t row = cursor_y / font->height;
    console_cells[row * console_columns + column] = c;

    rect_t r = {
        .x = cursor_x,
        .y = cursor_y,
        .w = font->width,
        .h = font->height
    };
    copy_rect(background, buffer_out, r.x, r.y, r.x, r.y, r.w, r.h);
    draw_psf1_char(*buffer_out, font, cursor_x, cursor_y, c, textcolor);
    cursor_x += font->width;
    copy_rect(buffer_out, &hardware_buffer, r.x, r.y, r.x, r.y, r.w, r.h);
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
    SCROLL_Y1 = buffer_out->height - 16;

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
    if (!background || !buffer_out) {
        return;
    }

    image_blit(background, buffer_out, 0, 0);
    image_blit(background, &hardware_buffer, 0, 0);

    if (console_cells) {
        memset(console_cells, ' ', console_columns * console_rows);
    }
    cursor_x = 0;
    cursor_y = 0;
}

void compositor_main(void)
{
    compositor_init();
    if (!buffer_out) {
        return;
    }

    struct color_rgba bg = {.r = 30, .g = 30, .b = 30, .a = 255};
    struct image *img_raw = bmp_load_image_alloc("/sys/dat/i386-theme/wallpaper.bmp", &g_pixfmt);

    background = image_create(hardware_buffer.width, hardware_buffer.height, &g_pixfmt);
    if (!background) {
        serial_print("ERR: failed to create compositor background\n");
        image_destroy(img_raw);
        return;
    }

    if (!img_raw || !image_scale_nearest(img_raw, background)) {
        serial_print("Wallpaper unavailable; using solid background\n");
        image_clear(background, bg);
    }
    image_destroy(img_raw);

    psf_font_t *font = &g_theme.font;
    if (!font->width || !font->height) {
        serial_print("ERR: compositor font unavailable\n");
        return;
    }

    console_columns = buffer_out->width / font->width - font->width;
    console_rows = buffer_out->height / font->height - font->height;
    if (!console_columns || !console_rows ||
        console_rows > 0xFFFFFFFFu / console_columns) {
        serial_print("ERR: framebuffer is too small for compositor text\n");
        return;
    }
    console_cells = kmalloc(console_columns * console_rows);
    if (!console_cells) {
        serial_print("ERR: failed to allocate compositor text buffer\n");
        return;
    }
    memset(console_cells, ' ', console_columns * console_rows);
    
    struct color_rgba textcolor = {255,255,255,255};
    
    serial_print("lets get started\n");

    compositor_clear_screen();
    while (1) {
        char c = read_tty_output(get_active_tty());

        if (c == '\b') {
            if (cursor_x > SCROLL_X0) {
                cursor_x -= font->width;
            } else if (cursor_y > SCROLL_Y0) {
                cursor_y -= font->height;
                cursor_x = (console_columns - 1) * font->width;
            } else {
                continue;
            }

            uint32_t column = cursor_x / font->width;
            uint32_t row = cursor_y / font->height;
            console_cells[row * console_columns + column] = ' ';
            copy_rect(background, buffer_out, cursor_x, cursor_y,
                      cursor_x, cursor_y, font->width, font->height);
            copy_rect(buffer_out, &hardware_buffer, cursor_x, cursor_y,
                      cursor_x, cursor_y, font->width, font->height);
        } else {
            if (c == '\n' ||
                (uint32_t)cursor_x + font->width > (uint32_t)SCROLL_X1) {
                compositor_advance_line(font);
                if (c == '\n') {
                    continue;
                }
            }
            compositor_put_char(font, (uint8_t)c, textcolor);
        }

    }
}