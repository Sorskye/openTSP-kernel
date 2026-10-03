#include "serial.h"
#include "io.h"
#include "spinlock.h"
#include "string.h"

static int serial_ready() {
    return inb(0x3F8 + 5) & 0x20;
}

void serial_write(char c){
    while (!serial_ready());
    outb(0x3F8,c);
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