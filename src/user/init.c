#include <stdint.h>

extern void syscall_write(const char *msg);
extern void syscall_clear();

void user_init()
{
    syscall_clear();
    syscall_write("Hello from USER MODE!\n");
    syscall_write("Syscalls are working.\n");

    while (1)
    {
        __asm__ __volatile__("hlt");
    }
}
