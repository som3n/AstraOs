#include <stdint.h>
#include "kernel/print.h"
#include "kernel/syscall_api.h"

static uint8_t user_stack[4096] __attribute__((aligned(16)));

void user_main()
{
    // In ring 3, direct hardware I/O (e.g. VGA cursor port writes inside `print`)
    // is privileged and will raise #GP. Use syscalls instead.
    sys_write("\n[USERMODE] Entered user mode successfully!\n");

    while (1)
    {
        // HLT is privileged (CPL must be 0). In ring 3 it triggers #GP(0).
        __asm__ __volatile__("pause");
    }
}

void switch_to_user_mode()
{
    print("\nSwitching to user mode...\n");

    uint32_t user_stack_top = (uint32_t)user_stack + sizeof(user_stack);

    __asm__ __volatile__(
        "cli \n"

        "mov $0x23, %%ax \n"
        "mov %%ax, %%ds \n"
        "mov %%ax, %%es \n"
        "mov %%ax, %%fs \n"
        "mov %%ax, %%gs \n"

        "pushl $0x23 \n"
        "pushl %0 \n"
        "pushf \n"
        "pushl $0x1B \n"
        "pushl %1 \n"

        "iret \n"
        :
        : "r"(user_stack_top), "r"(user_main)
        : "ax"
    );
}
