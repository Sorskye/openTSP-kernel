
//libs
#include "types.h"
#include "stdio.h"

#include "task.h"
#include "tty.h"
#include "string.h"
#include "fs.h"
#include "serial.h"
#include "memory.h"
// drivers



void printf(const char *fmt, ...) {
    
    size_t out_i = 0;
    size_t out_size = 1024;
    char out[out_size];

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
                        out_i += hex32_to_str(va_arg(args, unsigned int), out + out_i, width);
                    break;
                }

                case 's': {
                    const char *s = va_arg(args, const char *);
                    if (!s) s = "(null)";
                    while (*s && out_i < out_size - 1)
                        out[out_i++] = *s++;
                    break;
                }

                case 'c': {
                    if (out_i < out_size - 1)
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

    if (current_task && current_task->tty) {
        tty_write_line(current_task->tty, out);
    }

    va_end(args);
}



int fopen(const char* path, size_t pathlen,char* flags){
    return sys_open(path,pathlen,0);
}

void fclose(int fd){
    sys_close(fd);
}

char* fread(int fd, size_t* out_size) {
    if (out_size) *out_size = 0;

    if (fd < 0){
        serial_print("fd less than zero\n");
        return NULL;
    }

    file_t* file = current_process->fd_table[fd];
    if (!file || !file->inode){
        serial_print("no file or file inode\n");
        return NULL;
    }
    size_t size = file->inode->size;

    char* buffer = kzalloc(size + 1);
    if (!buffer){
        serial_print("no buffer\n");
        return NULL;
    }

    size_t total = 0;
    while (total < size) {
        int n = sys_read(fd, buffer + total, size - total);
        if (n <= 0) break;
        total += (size_t)n;
    }

    buffer[total] = '\0';

    *out_size = total;
    return buffer;
}

char* fgets(int fd, char* buf, size_t max_len) {
    if (max_len == 0) return NULL;

    size_t i = 0;

    while (i < max_len - 1) {
        char c;
        //should be ssize_t
        int n = sys_read(fd, &c, 1);

        if (n <= 0) break; // EOF

        buf[i++] = c;

        if (c == '\n') break;
    }

    if (i == 0) return NULL;

    buf[i] = '\0';
    return buf;
}