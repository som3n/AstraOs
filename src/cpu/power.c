#include "cpu/power.h"
#include "drivers/ports.h"

void cpu_halt() {
    __asm__ __volatile__("cli");
    while (1) {
        __asm__ __volatile__("hlt");
    }
}

void cpu_reboot() {
    // Keyboard controller reset command
    outb(0x64, 0xFE);

    // If reboot fails, halt
    cpu_halt();
}
