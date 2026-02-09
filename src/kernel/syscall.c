#include "kernel/syscall.h"
#include "cpu/isr.h"
#include "kernel/print.h"
#include "vga.h"

static void syscall_handler(registers_t *r)
{
    uint32_t syscall_num = r->eax;

    if (syscall_num == SYS_WRITE)
    {
        char *msg = (char *)r->ebx;
        print(msg);
        r->eax = 0; // return success
    }
    else if (syscall_num == SYS_CLEAR)
    {
        clear_screen();
        r->eax = 0;
    }
    else
    {
        print("\n[SYSCALL] Unknown syscall\n");
        r->eax = (uint32_t)-1;
    }
}

void syscall_init()
{
    isr_register_handler(0x80, syscall_handler);
    print("\nSyscall system ready (int 0x80)\n");
}
