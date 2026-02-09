#include "cpu/tss.h"
#include "cpu/gdt.h"
#include "string.h"

static tss_entry_t tss;

extern void tss_flush();

void tss_set_kernel_stack(uint32_t stack)
{
    tss.esp0 = stack;
}

void tss_install(uint32_t kernel_stack_top)
{
    memset(&tss, 0, sizeof(tss_entry_t));

    tss.ss0 = 0x10; // kernel data segment selector
    tss.esp0 = kernel_stack_top;

    // Not used for our syscall/interrupt stack switch, but keep consistent.
    tss.cs = 0x1B; // user code selector (GDT entry 3 | RPL3)
    tss.ss = 0x23; // user data selector (GDT entry 4 | RPL3)
    tss.ds = 0x23;
    tss.es = 0x23;
    tss.fs = 0x23;
    tss.gs = 0x23;

    // no IO permissions
    tss.iomap_base = sizeof(tss_entry_t);

    // Install TSS into GDT entry 5
    gdt_set_tss((uint32_t)&tss, sizeof(tss_entry_t) - 1);

    // Load TR register
    tss_flush();
}
