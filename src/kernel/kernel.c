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
#include "memory/paging.h"
#include "kernel/syscall.h"
#include "cpu/tss.h"
#include "cpu/usermode.h"

void kernel_main()
{
    clear_screen();
    print("Booting AstraOS...\n");

    gdt_init();
    idt_init();
    isr_install();
    pic_remap();
    irq_install();

    timer_init(100);
    keyboard_init();

    extern uint32_t kernel_end;
    kmalloc_init((uint32_t)&kernel_end + 0x1000);

    uint32_t kernel_stack_top = (uint32_t)&kernel_end + 0x4000;
    tss_install(kernel_stack_top);

    paging_init();
    syscall_init();

    extern void switch_to_user_mode();

    switch_to_user_mode();

    enable_interrupts();

    shell_init();

    while (1)
    {
    }
}
