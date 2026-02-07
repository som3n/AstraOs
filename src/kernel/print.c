#include "kernel/print.h"
#include "vga.h"

void print_uint(uint32_t num) {
    char buf[16];
    int i = 0;

    if (num == 0) {
        print("0");
        return;
    }

    while (num > 0) {
        buf[i++] = '0' + (num % 10);
        num /= 10;
    }

    buf[i] = '\0';

    // reverse print
    for (int j = i - 1; j >= 0; j--) {
        print_char(buf[j]);
    }
}
