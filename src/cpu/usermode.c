#include <stdint.h>
#include "kernel/print.h"

static volatile int last_exit_code = 0;
static volatile uint32_t saved_esp = 0;
static volatile uint32_t saved_ebp = 0;
static volatile uint32_t saved_ebx = 0;
static volatile uint32_t saved_esi = 0;
static volatile uint32_t saved_edi = 0;

// Assembly trampoline:
// - Saves kernel callee-saved regs + esp/ebp
// - iret's into ring3 user_init
// - SYS_EXIT jumps back to usermode_resume, which restores regs and returns via ret
__attribute__((naked)) static int switch_to_user_mode_trampoline(uint32_t user_stack_top, void *entry)
{
    (void)user_stack_top;
    (void)entry;

    __asm__ __volatile__(
        // Grab args before we change the stack with pushes.
        // Stack on entry: [ret][arg0=user_stack_top][arg1=entry]
        "movl 4(%esp), %ecx \n"    // user_stack_top
        "movl 8(%esp), %edx \n"    // entry

        // Save kernel call context (so we can return cleanly later).
        "movl %esp, saved_esp \n"
        "movl %ebp, saved_ebp \n"
        "movl %ebx, saved_ebx \n"
        "movl %esi, saved_esi \n"
        "movl %edi, saved_edi \n"

        "cli \n"

        // Load user data segments.
        "mov $0x23, %ax \n"
        "mov %ax, %ds \n"
        "mov %ax, %es \n"
        "mov %ax, %fs \n"
        "mov %ax, %gs \n"

        // Build an iret frame to ring 3: SS, ESP, EFLAGS, CS, EIP.
        "pushl $0x23 \n"          // SS (user data, RPL3)
        "pushl %ecx \n"           // user_stack_top

        // EFLAGS with IF set.
        "pushf \n"
        "popl %eax \n"
        "orl $0x200, %eax \n"
        "pushl %eax \n"

        "pushl $0x1B \n"          // CS (user code, RPL3)
        "pushl %edx \n"           // entry

        "iret \n"

        ".globl usermode_resume \n"
        "usermode_resume: \n"
        // Restore kernel segments (defensive; syscall stub may have changed them).
        "mov $0x10, %ax \n"
        "mov %ax, %ds \n"
        "mov %ax, %es \n"
        "mov %ax, %fs \n"
        "mov %ax, %gs \n"

        // Restore saved regs and return to caller of switch_to_user_mode().
        "movl saved_edi, %edi \n"
        "movl saved_esi, %esi \n"
        "movl saved_ebx, %ebx \n"
        "movl saved_ebp, %ebp \n"
        "movl saved_esp, %esp \n"
        "movl last_exit_code, %eax \n"
        "sti \n"
        "ret \n"
    );
}

void usermode_exit(int code)
{
    last_exit_code = code;
    __asm__ __volatile__("jmp usermode_resume" : : : "memory");
    __builtin_unreachable();
}

int switch_to_user_mode(uint32_t entry_eip, uint32_t user_stack_top)
{
    print("\nSwitching to user mode...\n");
    return switch_to_user_mode_trampoline(user_stack_top, (void *)entry_eip);
}
