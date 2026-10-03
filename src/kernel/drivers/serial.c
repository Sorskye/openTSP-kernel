#include "serial.h"
#include "io.h"
#include "spinlock.h"
#include "string.h"
#include "debug.h"
#include "vfs.h"
#include "main.h"


static bool DEBUG_TO_VIDEO = false;
static uint8_t serial_count = 0;


static int serial_ready() {
    return inb(0x3F8 + 5) & 0x20;
}

void serial_write(char c){
    while (!serial_ready());
    outb(0x3F8,c);
    if(!DEBUG_TO_VIDEO){
        return;
    }
    DebugPutChar(c);
}

void serial_mark(){
    serial_write('*');
    serial_write('*');
    serial_write('*');
    serial_write(' ');
    serial_write('M');
    serial_write('A');
    serial_write('R');
    serial_write('K');
    serial_write('\n');

}

void serial_print(const char *fmt, ...) {
    
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

    for(int i=0; i<= out_i; i++){
        char c = out[i];
        serial_write(c);
    }
    
    
   va_end(args);
}

void serial_print_hex(uint32_t value, int width){
    char hex_chars[] = "0123456789abcdef";
    char buffer[9];
    int i = 0;


    do {
        buffer[i++] = hex_chars[value & 0xF];
        value >>= 4;
    } while (value && i < 8);

    while (i < width && i < 8)
        buffer[i++] = '0';

    for (int j = i - 1; j >= 0; j--)
        serial_write(buffer[j]);

}


// user api
int serial_write_user(struct file *file, const void *buf, size_t size){
    const char *bytes = buf;

    (void)file;
    if (!bytes && size != 0)
        return -1;

    for (size_t i = 0; i < size; i++)
        serial_write(bytes[i]);

    return (int)size;
}

struct file_ops serial_file_ops = {
    .read = NULL,
    .write = serial_write_user,
    .lseek = NULL,
    .open = NULL,
    .close = NULL
};


struct inode* create_serial_inode(){

    uint8_t serial_number = serial_count;

    // prepare filename
    uint32_t SerCountStrLen = (serial_number / 10);
    if(SerCountStrLen == 0){SerCountStrLen = 1;}

    char filename[3 + SerCountStrLen + 1];

    strcpy(filename, "COM");
    itoa((int)serial_number, filename + 3, 10);
    
    char dirprefix[9+SerCountStrLen];
    strcpy(dirprefix, "/dev/");
    strcpy(dirprefix+5, filename);

    int ret = sys_create(dirprefix);
    struct inode* inode = vfs_lookup(dirprefix, root_inode);

    if(!inode){
        serial_print("failed to inode file for: %s\n", dirprefix);
        return NULL;
    }

    inode->file_ops = &serial_file_ops;
    serial_count++;

    serial_print("new serial: %s\n",dirprefix);
    return inode;
}

void init_serial(){
    create_serial_inode();
    return;
}




BOOT static int serial_ready_bs() {
    return inb_bs(0x3F8 + 5) & 0x20;
}

BOOT void serial_write_bs(char c){
    while (!serial_ready_bs());
    outb_bs(0x3F8,c);
}

BOOT void serial_print_bs(const char *fmt, ...) {
    
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

    for(int i=0; i<= out_i; i++){
        char c = out[i];
        serial_write_bs(c);
    }
    
    
   va_end(args);
}
