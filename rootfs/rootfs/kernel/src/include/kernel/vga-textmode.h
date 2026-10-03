
#ifndef VGATEXT_H
#define VGATEXT_H
#include "types.h"



enum VGA_COLOR{
    VGA_BLACK = 0,
    VGA_BLUE = 1,
    VGA_GREEN = 2,
    VGA_CYAN = 3,
    VGA_RED = 4,
    VGA_MAGENTA = 5,
    VGA_BROWN = 6,
    VGA_LIGHT_GREY = 7,
    VGA_DARK_GREY = 8,
    VGA_LIGHT_BLUE = 9,
    VGA_LIGHT_GREEN = 10,
    VGA_LIGHT_CYAN = 11,
    VGA_LIGHT_RED = 12,
    VGA_PINK = 13,
    VGA_YELLOW = 14,
    VGA_WHITE = 15
};

static const uint8_t vga_default_palette[16][3] = {
    {0x00, 0x00, 0x00}, // 0 black
    {0x00, 0x00, 0xAA}, // 1 blue
    {0x00, 0xAA, 0x00}, // 2 green
    {0x00, 0xAA, 0xAA}, // 3 cyan
    {0xAA, 0x00, 0x00}, // 4 red
    {0xAA, 0x00, 0xAA}, // 5 magenta
    {0xAA, 0x55, 0x00}, // 6 brown
    {0xAA, 0xAA, 0xAA}, // 7 light gray
    {0x55, 0x55, 0x55}, // 8 dark gray
    {0x55, 0x55, 0xFF}, // 9 bright blue
    {0x55, 0xFF, 0x55}, // 10 bright green
    {0x55, 0xFF, 0xFF}, // 11 bright cyan
    {0xFF, 0x55, 0x55}, // 12 bright red
    {0xFF, 0x55, 0xFF}, // 13 bright magenta
    {0xFF, 0xFF, 0x55}, // 14 yellow
    {0xFF, 0xFF, 0xFF}  // 15 white
};


extern uint16_t* vga_framebuffer;

// vga framebuffers
void vga_putc(char c, int x, int y);
void vga_set_color(enum VGA_COLOR FG, enum VGA_COLOR BG);
void vga_set_cursor(uint16_t x, uint16_t y);
void vga_clear();


void vga_disable_cursor();
void vga_enable_cursor(uint8_t cursor_start, uint8_t cursor_end);
void vga_init();
void vga_refresh();
void vga_push_framebuffer(uint16_t* framebuffer);

int vga_get_cursor();

extern const uint8_t GRID_WIDTH, GRID_HEIGHT;

// extern vga framebuffers
uint16_t* alloc_framebuffer();
uint8_t vga_make_attr_blink(enum VGA_COLOR FG, enum VGA_COLOR BG, bool blink);
uint8_t vga_make_attr(enum VGA_COLOR FG, enum VGA_COLOR BG, bool blink);


#endif