#include "drivers/pic.h"
#include "drivers/ports.h"

#define PIC1_COMMAND 0x20
#define PIC1_DATA    0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA    0xA1

void pic_remap() {
    outb(PIC1_COMMAND, 0x11);
    outb(PIC2_COMMAND, 0x11);

    outb(PIC1_DATA, 0x20);
    outb(PIC2_DATA, 0x28);

    outb(PIC1_DATA, 0x04);
    outb(PIC2_DATA, 0x02);

    outb(PIC1_DATA, 0x01);
    outb(PIC2_DATA, 0x01);

    outb(PIC1_DATA, 0x0);
    outb(PIC2_DATA, 0x0);
}

void pic_send_eoi(int irq) {
    if (irq >= 8) {
        outb(PIC2_COMMAND, 0x20);
    }
    outb(PIC1_COMMAND, 0x20);
}

void pic_clear_mask(unsigned char irq_line) {
    unsigned short port;
    unsigned char value;

    if (irq_line < 8) {
        port = 0x21;
    } else {
        port = 0xA1;
        irq_line -= 8;
    }

    value = inb(port) & ~(1 << irq_line);
    outb(port, value);
}
