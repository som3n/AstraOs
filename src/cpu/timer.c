#include "cpu/timer.h"
#include "cpu/irq.h"
#include "drivers/ports.h"

static volatile uint32_t ticks = 0;
static uint32_t timer_frequency = 0;

static void timer_callback(registers_t *r) {
    (void)r;
    ticks++;
}

void timer_init(uint32_t frequency) {
    timer_frequency = frequency;
    irq_register_handler(0, timer_callback);

    uint32_t divisor = 1193180 / frequency;

    outb(0x43, 0x36);
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));
}

uint32_t timer_get_ticks() {
    return ticks;
}

void timer_sleep(uint32_t seconds) {
    uint32_t start = timer_get_ticks();
    uint32_t target = seconds * timer_frequency;

    while ((timer_get_ticks() - start) < target) {
        // Enable interrupts and halt until next interrupt
        __asm__ __volatile__("sti; hlt");
    }
}
