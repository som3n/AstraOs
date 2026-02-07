#include "cpu/isr.h"

void enable_interrupts() {
    __asm__ __volatile__("sti");
}
