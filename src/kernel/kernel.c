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
#include "kernel/print.h"
#include "kernel/elf32.h"
#include "string.h"
#include "kernel/exec.h"

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

    enable_interrupts();

    int exit_code = kernel_exec_elf("/BIN/INIT.ELF");
    if (exit_code < 0)
    {
        print("\nELF load failed: /BIN/INIT.ELF\n");
    }
    else
    {
        print("\n[USERMODE] exited with code ");
        print_uint((uint32_t)exit_code);
        print("\n");
    }

    shell_init();

    while (1)
    {
    }
}
