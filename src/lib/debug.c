
#include "IDT.h"
#include "debug.h"
#include "symbols.h"
#include "task.h"
#include "serial.h"
#include "vga-textmode.h"
#include "string.h"
#include "lpcspeak.h"
#include "io.h"
#include "psf.h"
#include "video.h"
#include "framebuffer.h"
#include "TSS.h"
#include "syscall.h"

#include "main.h"

static bool DEBUG_INIT = false;

static uint32_t BugCheckCursorX = 1;
static uint32_t BugCheckCursorY = 1;

static uint32_t DebugCursorX = 1;
static uint32_t DebugCursorY = 1;

static struct framebuffer HardwareBuffer = {0};
static struct image BugCheckImage = {0};
struct image DebugImage = {0};

static uint32_t FB_Width = 0;
static uint32_t FB_Height = 0;
static struct pixel_format FB_FMT = {0};
static uint32_t FB_Pitch = 0;

static psf_font_t Font = {0};
static volatile uint32_t panic_active = 0;

static struct color_rgba BugCheckFontColor = {
    .r = 255,
    .g = 255,
    .b = 255,
    
};

static struct color_rgba BugCheckBGColor = {
    .r = 0,
    .g = 0,
    .b = 255,
};



static struct color_rgba DebugFontColor = {
    .r = 255,
    .g = 255,
    .b = 255,
    
};

static struct color_rgba DebugBGColor = {
    .r = 00,
    .g = 00,
    .b = 00,
};




void init_debug(const char* font_path)
{
    HardwareBuffer = get_framebuffer();
    serial_print("debug fb at 0x%x",HardwareBuffer);
    FB_Width = HardwareBuffer.width;
    FB_Height = HardwareBuffer.height;
    FB_FMT = HardwareBuffer.fmt;
    FB_Pitch = HardwareBuffer.pitch;

   
   
    BugCheckImage = image_wrap(HardwareBuffer.addr, FB_Width, FB_Height, FB_Pitch, &FB_FMT);
    DebugImage =  image_wrap(HardwareBuffer.addr, FB_Width, FB_Height, FB_Pitch, &FB_FMT);
    image_clear(&BugCheckImage, BugCheckBGColor);
    image_clear(&DebugImage, DebugBGColor);

    uint8_t psf_loaded = load_psf1_font(&Font, font_path);


    if (FB_Width <= 0 && FB_Height <= 0){
        serial_print("[!!!] failed to setup visual debugger: framebuffer too small (x%d y%d)\n",FB_Width, FB_Height);
        
    }

    if(psf_loaded != 1){
        serial_print("[!!!] failed to setup visual debugger: psf failed to load\n");
    }
    
    DEBUG_INIT = true;

    serial_print("debug initialized");
    return;
}

BOOT void KeHaltBS(){
    while (true){
        asm volatile ("hlt");
    }
}

void KeHalt(){
    while (true){
        asm volatile ("hlt");
    }
}

const char* lookup_symbol(uint32_t addr) {
    const char* result = "unknown";
    uint32_t best = 0;

    for (int i = 0; i < kernel_symbol_count; i++) {
        uint32_t sym = kernel_symbols[i].addr;

        if (sym <= addr && sym >= best) {
            best = sym;
            result = kernel_symbols[i].name;
        }
    }

    return result;
}

static uint32_t lookup_symbol_address(uint32_t addr) {
    uint32_t best = 0;

    for (int i = 0; i < kernel_symbol_count; i++) {
        uint32_t sym = kernel_symbols[i].addr;

        if (sym <= addr && sym >= best) {
            best = sym;
        }
    }

    return best;
}

void BugCheckPrintf(const char *fmt, ...) {
    

    char out[1024];
    
    size_t out_i = 0;

    va_list args;
    va_start(args, fmt);

    for (size_t i = 0; fmt[i] != '\0'; ++i) {

        if (fmt[i] == '%') {
            i++;

            int width = 0;
            if (fmt[i] == '0') {
                i++;
                while (fmt[i] >= '0' && fmt[i] <= '9') {
                    width = width * 10 + (fmt[i] - '0');
                    i++;
                }
            }

            int is_ll = 0;
            if (fmt[i] == 'l' && fmt[i+1] == 'l') {
                is_ll = 1;
                i += 2;
            }

            switch (fmt[i]) {

                case 'd': {
                    if (is_ll)
                        out_i += i64_to_str(va_arg(args, long long), out + out_i);
                    else
                        out_i += int_to_str(va_arg(args, int), out + out_i);
                    break;
                }

                case 'u': {
                    if (is_ll)
                        out_i += u64_to_str(va_arg(args, unsigned long long), out + out_i);
                    else
                        out_i += uint_to_str(va_arg(args, unsigned int), out + out_i);
                    break;
                }

                case 'x': {
                    if (is_ll)
                        out_i += hex64_to_str(va_arg(args, unsigned long long), out + out_i, width);
                    else
                        out_i += hex32_to_str(va_arg(args, uint32_t), out + out_i, width);
                    break;
                }

                case 's': {
                    const char* s = va_arg(args, const char*);
                    while (*s) out[out_i++] = *s++;
                    break;
                }

                case 'c': {
                    out[out_i++] = (char)va_arg(args, int);
                    break;
                }

                case '%': {
                    out[out_i++] = '%';
                    break;
                }

                default: {
                    out[out_i++] = '%';
                    out[out_i++] = fmt[i];
                }
            }

        } else {
            out[out_i++] = fmt[i];
        }

        if (out_i >= sizeof(out) - 1)
            break;
    }

    out[out_i] = '\0';
    va_end(args);

    serial_print(out);

    // dont write to screen if debugger is not initialized
    if(!DEBUG_INIT){
        return;
    }
    
    for (int i = 0; out[i] != '\0'; i++){
        char c = out[i];
        if (c == '\n'){
            BugCheckCursorX = 1;
            BugCheckCursorY++;
        }else{
            draw_psf1_char(BugCheckImage, &Font, (BugCheckCursorX * Font.width), (BugCheckCursorY * Font.height), c, BugCheckFontColor);
            BugCheckCursorX++;
        }
       
    }

    return;
}

void DebugPutChar(char c){
    
    if(!DEBUG_INIT){
        return;
    }

    if (c == '\n'){
        DebugCursorX = 1;
        DebugCursorY++;
    }else{
        draw_psf1_char(DebugImage, &Font, (DebugCursorX * Font.width), (DebugCursorY * Font.height), c, DebugFontColor);
        DebugCursorX++;
    }
}


bool keyboard_poll_has_char(void) {
    return (inb(0x64) & 0x01) != 0;
}
uint8_t keyboard_get_sc(void) {
   
}
void print_stack_trace(cpu_regs_with_int_code_t *regs)
{
    uint32_t stack_low =
        (uint32_t)(uintptr_t)current_task->stack;

    uint32_t stack_high =
        stack_low + KERNEL_STACK_SIZE;

    uint32_t ebp = regs->ebp;

    BugCheckPrintf("call trace\n");

    BugCheckPrintf(
        "task stack = %08x - %08x\n",
        stack_low,
        stack_high
    );

    

    uint32_t symbol_address =
        lookup_symbol_address(regs->eip);

    if (symbol_address) {
        BugCheckPrintf(
            "#0 <%s+0x%x>",
            lookup_symbol(regs->eip),
            regs->eip - symbol_address
        );
    }

    BugCheckPrintf("\n");

    for (uint32_t i = 0; i < 32; i++) {

        if (ebp & 3)
            BugCheckPrintf("fail: EBP not aligned\n");
            break;

        if (ebp < stack_low ||
            ebp > stack_high - 8)
            BugCheckPrintf("fail: EBP out of stack bounds\n");
            break;

        uint32_t *frame =
            (uint32_t *)(uintptr_t)ebp;

        uint32_t next_ebp = frame[0];
        uint32_t ret      = frame[1];

        BugCheckPrintf(
            "#%02u frame=%08x ret=%08x",
            i + 1,
            ebp,
            ret
        );

        symbol_address = lookup_symbol_address(ret);

        if (symbol_address) {
            BugCheckPrintf(
                "# <%s+0x%x>",
                lookup_symbol(ret),
                ret - symbol_address,
                i+1
            );
        }

        BugCheckPrintf("\n");

        if (next_ebp == 0)
            break;

        if (next_ebp <= ebp)
            break;

        if (next_ebp & 3)
            break;

        if (next_ebp < stack_low ||
            next_ebp > stack_high - 8)
            break;

        ebp = next_ebp;
    }
    BugCheckPrintf("\n");

}

void PrintEFlags(uint32_t eflags)
{
    BugCheckPrintf("EFLAGS = 0x%08x\n", eflags);

    BugCheckPrintf("CF = %d | ",  (eflags >> 0) & 1);
    BugCheckPrintf("PF = %d | ",  (eflags >> 2) & 1);
    BugCheckPrintf("AF = %d |",  (eflags >> 4) & 1);
    BugCheckPrintf("ZF = %d | ",  (eflags >> 6) & 1);
    BugCheckPrintf("SF = %d | ",  (eflags >> 7) & 1);
    BugCheckPrintf("TF = %d\n",  (eflags >> 8) & 1);
    BugCheckPrintf("IF = %d | ",  (eflags >> 9) & 1);
    BugCheckPrintf("DF = %d | ",  (eflags >> 10) & 1);
    BugCheckPrintf("OF = %d |",  (eflags >> 11) & 1);

    BugCheckPrintf("IOPL = %d | ",  (eflags >> 12) & 3);

    BugCheckPrintf("NT = %d | ",  (eflags >> 14) & 1);
    BugCheckPrintf("RF = %d\n",  (eflags >> 16) & 1);
    BugCheckPrintf("VM = %d | ",  (eflags >> 17) & 1);
    BugCheckPrintf("AC = %d | ",  (eflags >> 18) & 1);
    BugCheckPrintf("VIF = %d \n",  (eflags >> 19) & 1);
    BugCheckPrintf("VIP = %d | ",  (eflags >> 20) & 1);
    BugCheckPrintf("ID = %d\n\n",  (eflags >> 21) & 1);
}


void PrintRegisters(cpu_regs_with_int_code_t *regs)
{   
    uint32_t ss;
    asm volatile("mov %%ss, %0" : "=r"(ss));
    BugCheckPrintf("SS: 0x%08x\n",ss);
    BugCheckPrintf("CS: 0x%08x | ",regs->cs);
    BugCheckPrintf("DS: 0x%08x\n",regs->ds);
    BugCheckPrintf("ES: 0x%08x | ",regs->es);
    BugCheckPrintf("GS: 0x%08x\n",regs->gs);

    BugCheckPrintf("EAX: 0x%08x | ",regs->eax);
    BugCheckPrintf("EBX: 0x%08x\n",regs->ebx);
    BugCheckPrintf("ECX: 0x%08x | ",regs->ecx);
    BugCheckPrintf("EDX: 0x%08x\n",regs->edx);
    BugCheckPrintf("ESI: 0x%08x | ",regs->esi);
    BugCheckPrintf("EDI: 0x%08x\n",regs->edi);
    BugCheckPrintf("ESP: 0x%08x | ",regs->esp);
    BugCheckPrintf("EBP: 0x%08x\n",regs->ebp);
    
}

void PrintPageData(uint32_t intNum, uint32_t errCode)
{
    uint32_t cr2;
    asm volatile("mov %%cr2, %0" : "=r"(cr2));

    if(intNum == 14){
        

        if(errCode & 4){
            BugCheckPrintf("*** USER ");
        }else{
            BugCheckPrintf("*** SUPERVISOR ");
        }

        if(errCode & 2){
            BugCheckPrintf("attempted page write ");
        }else{
            BugCheckPrintf("attempted page read ");
        }

    
        if(!(errCode & 1)){
            BugCheckPrintf("on a non-present page\n\n");
        }else{
            BugCheckPrintf("and violated page protection rules\n\n");
        }

        BugCheckPrintf("PF err: 0x%x\n",errCode);
        BugCheckPrintf("CR3: 0x%08x\n", get_page_directory());
    
    }
    


    uint32_t pde_index = (cr2 >> 22) & 0x3FF;
    uint32_t pte_index = (cr2 >> 12) & 0x3FF;

    BugCheckPrintf("addr=0x%x pde_idx=0x%x pte_idx=0x%x\n", cr2, pde_index, pte_index);
    
}



void KeBugCheck(uint32_t code, char* caller_msg, cpu_regs_with_int_code_t *regs){
    
    __asm__ volatile("cli");

    if (__sync_lock_test_and_set(&panic_active, 1)) {
       // image_clear(&BugCheckImage, BugCheckBGColor);
        BugCheckPrintf("** SYSTEM STOPPED\n");
        BugCheckPrintf("PANIC: RECURSIVE_FAULT\n");
       
        for (;;) {
            __asm__ volatile("hlt");
        }
    }

    serial_print("PANIC\n");
    if (DEBUG_INIT) {
       speaker_sound_panic();
    }
    
    if(DEBUG_INIT){
    image_clear(&BugCheckImage, BugCheckBGColor);
    }
    
    char* BugCheck_msg = caller_msg ? caller_msg : BugCheckMessages[code];
    uint8_t ring = regs ? (regs->cs & 0x3) : 0;

    BugCheckPrintf("** FATAL | STOP: %s\n",BugCheck_msg);
    
    
    // if unhandled isr, print isr message
    if(code == 1 && regs!=NULL){
        BugCheckPrintf("EXCEPTION: %s\n", interruptMessages[regs->intNum]);
    }
 
    if (current_process) {
        BugCheckPrintf("\nWhile executing process: %s | PID=%d\n", current_process->name, current_process->pid);
    }
    if (current_task) {
        BugCheckPrintf("On task with TID=%d | SP=%x | STATE=%d (ring %d)\n", current_task->tid, current_task->esp, current_task->state, ring);
    }
    if(inSyscall > -1){
        BugCheckPrintf("SUPERVISOR in syscall context (%d:%s)\n\n", inSyscall,syscallDefineName(inSyscall));
    }


    if(regs && (regs->eip != 0 || regs->esp != 0))
    {
        BugCheckPrintf("\nFAULT AT: %08x - %s\n",regs->eip, lookup_symbol(regs->eip));
        PrintRegisters(regs);
        PrintEFlags(regs->eflags);
        print_stack_trace(regs);
        PrintPageData(regs->intNum, regs->errCode);
    }


    BugCheckPrintf("\n");
    BugCheckPrintf("\n");
    BugCheckPrintf("press TAB to disable pc speaker\n");
    BugCheckPrintf("press R to reboot\n");
   
   // print_stack_trace(regs);
 

    for (;;) {
        if (keyboard_poll_has_char()) {
             uint8_t sc = inb(0x60);
            // TAB turn off speaker
            if (sc == 0x0F ){
                uint8_t tmp = inb(0x61) & 0xFC; 
                outb(0x61, tmp);
            // R reboot
            }else if(sc == 0x13){
                outb(0x64, 0xFE);
            }
        }
    }

    while(1){
        asm volatile("hlt");
    }
   
}


Char *interruptMessages[] = {

    "DIVISION_FAULT",       //0 
    "DEBUG",                  //1
    "NON_MASKABLE_INT", //2
    "BREAKPOINT",             //3
    "INTO_DETECTED_OVERFLOW", //4
    "OUT_OF_BOUND",          //5
    "INVALID_OPTCODE",         //6
    "NO_COPROCESSOR",         //7

    "DOUBLE_FAULT",                //8
    "COPROCESSOR_SEGMENT_OVERRUN", //9
    "BAD_TSS",                     //10
    "SEGMENT_NOT_PRESENT",         //11
    "STACK_FAULT",                 //12
    "GENERAL_PROTECTION",    //13
    "PAGE_FAULT", //14
    "UNKNOWN_INTERRUPT", //15

    "COPROCESSOR_FAULT", //16
    "ALIGNMENT_CHECK", //17
    "MACHINE_CHECK", //18
    "Reserved", //19
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",

    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
};

Char *irqMessages[] = {
    "SYSTEM_TIMER",       //0
    "KEYBOARD_PS/2",                  //1
    "INT_CONTROLLER", //2
    "SERIAL_CONTROLLER_2",             //3
    "SERIAL_CONTROLLER_1", //4
    "PARRALLEL_PORT_3_OR_ISA_SOUND",          //5
    "FLOPPY_CONTROLLER",         //6
    "PARRALLEL_PORT_1",         //7

    "RTC",                //8
    "ACPI", //9
    "PERIPHERAL_1",                     //10
    "PERIPHERAL_2",         //11
    "MOUSE_PS/2",                 //12
    "CO_PROCESSOR_OR_FLOATING_POINT_UNIT",    //13
    "ATA_PRIMARY",
    "ATA_SECONDARY",
};

Char *BugCheckMessages[] = {
    "**",                               //0 (custom message)
    "BUG_CHECK_FROM_UNHANDLED_ISR",     //1
    "BUG_CHECK_FROM_ISR",               //2
    "TASK_QUEUE_VIOLATION",             //3
    "ALLOC_NOT_PAGE_ALIGNED",           //4
    "ALLOC_RETURNED_PAGE_ZERO",         //5
    "HEAP_INIT_FAILURE",                //6
    "UNHANDLED_ZERO_DIVISION_BY_SUPERVISOR",           //7
    "SUPERVISOR_GENERAL_PROTECTION",        //8
    "UNHANDLED_SUPERVISOR_INVALID_OPTCODE",           //9
    "UNHANDLED_SUPERVISOR_PAGE_FAULT",                //10         
};
