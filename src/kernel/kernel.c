#include "vga.h"
#include "cpu/gdt.h"
#include "cpu/idt.h"
#include "cpu/irq.h"
#include "drivers/pic.h"
#include "cpu/isr.h"
#include "drivers/keyboard.h"
#include "cpu/timer.h"
#include "shell.h"
#include "memory/kmalloc.h"

void kernel_main() {
    clear_screen();
    print("Booting AstraOS...\n");

    gdt_init();
    idt_init();
    pic_remap();

    irq_install();

    timer_init(100);
    keyboard_init();

    kmalloc_init(0x1000000);

    enable_interrupts();

    shell_init();

    while (1) {
    }
}
