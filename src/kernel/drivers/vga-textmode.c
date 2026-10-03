
#include "types.h"
#include "vga-textmode.h"
#include "io.h"
#include "vmm.h"
#include "debug.h"
#include "spinlock.h"
#include "string.h"

static volatile uint16_t* vga_buff_addr = (uint16_t*)0xB8000;

const uint8_t GRID_WIDTH=80, GRID_HEIGHT=25;
#define VGA_SIZE (GRID_WIDTH * GRID_HEIGHT * sizeof(uint16_t))
static volatile uint8_t VGA_ENTRY_ATTR = VGA_WHITE | VGA_BLACK << 4;

spinlock_t vga_putc_lock = {0};
spinlock_t vga_refresh_lock = {0};

uint16_t* vga_framebuffer = NULL;

uint16_t* alloc_framebuffer(){
    uint16_t* fb = (uint16_t*)kmalloc(VGA_SIZE);
    memset(fb,0,VGA_SIZE);
    return fb;

}

void vga_push_framebuffer(uint16_t* framebuffer){
    cli();
    memcpy(vga_buff_addr, framebuffer, VGA_SIZE);
    sti();
}

void vga_refresh(){
    vga_push_framebuffer(vga_framebuffer);
}


void vga_enable_cursor(uint8_t cursor_start, uint8_t cursor_end)
{
	outb(0x3D4, 0x0A);
	outb(0x3D5, (inb(0x3D5) & 0xC0) | cursor_start);

	outb(0x3D4, 0x0B);
	outb(0x3D5, (inb(0x3D5) & 0xE0) | cursor_end);
}

void vga_disable_cursor()
{
	outb(0x3D4, 0x0A);
	outb(0x3D5, 0x20);
}

void vga_disable_blink(){
    outb(0x3C0, 0x10);
    outb(0x3C1, inb(0x3C1) & ~0x08 | 0x20);
}

void vga_enable_blink() {
    // Reset attribute controller flip-flop
    (void)inb(0x3DA);

    // Select register 0x10
    outb(0x3C0, 0x10);

    // Read current value
    uint8_t val = inb(0x3C1);

    // Enable blink (set bit 3), disable bright-background mode (clear bit 5)
    val |= 0x08;   // set blink enable
    val &= ~0x20;  // clear bright background

    // Reset flip-flop again
    (void)inb(0x3DA);

    // Write modified value back
    outb(0x3C0, val);
}

static inline void vga_load_default_palette() {
    outb(0x00, 0x3C8); // start at color index 0
    for (int i = 0; i < 16; i++) {
        outb(vga_default_palette[i][0] >> 2, 0x3C9); // VGA DAC is 6-bit per channel
        outb(vga_default_palette[i][1] >> 2, 0x3C9);
        outb(vga_default_palette[i][2] >> 2, 0x3C9);
    }
}

void vga_init(){
    vga_framebuffer = alloc_framebuffer();
    
    
    //vga_enable_blink();
  
   // vga_disable_blink();
    //vga_load_default_palette();
}




// hardware ops

void vga_set_cursor(uint16_t x, uint16_t y)
{
	uint16_t pos = y * GRID_WIDTH + x;

	outb(0x3D4, 0x0F);
	outb(0x3D5, (uint8_t) (pos & 0xFF));
	outb(0x3D4, 0x0E);
	outb(0x3D5, (uint8_t) ((pos >> 8) & 0xFF));

    return;
}


void vga_putc(char c, int x, int y){
    const size_t index = y * GRID_WIDTH + x;
  //  vga_framebuffer[index] = ((uint8_t)VGA_ENTRY_ATTR << 8) | (uint8_t)c;
}



void vga_set_color(enum VGA_COLOR FG, enum VGA_COLOR BG){
    VGA_ENTRY_ATTR = FG | BG << 4;
}

void vga_clear(){

    int tmp_cursor_x = 0;
    int tmp_cursor_y = 0;

    VGA_ENTRY_ATTR &= 0x7F;
    for(int x=0; x < GRID_WIDTH; x++){
        for(int y=0; y < GRID_HEIGHT; y++){
            vga_putc(' ', x, y);
        }
    }
}