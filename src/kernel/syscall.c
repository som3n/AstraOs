#include "kernel/syscall.h"
#include "cpu/idt.h"
#include "kernel/print.h"
#include "cpu/isr.h"

static void syscall_handler(registers_t *regs)
{
    // eax = syscall number
    // ebx, ecx, edx = arguments (common convention)
    switch (regs->eax)
    {
    case 0: // sys_write
        // ebx = pointer to string
        print((const char *)regs->ebx);
        break;

    default:
        print("\nUnknown syscall!\n");
        break;
    }
}

void syscall_init()
{
    // install syscall handler on interrupt 0x80
    register_interrupt_handler(0x80, syscall_handler);
}
