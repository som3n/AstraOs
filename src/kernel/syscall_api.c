#include "kernel/syscall_api.h"

int sys_write(const char *msg)
{
    int ret;
    __asm__ __volatile__(
        "mov $0, %%eax \n"
        "mov %1, %%ebx \n"
        "int $0x80 \n"
        "mov %%eax, %0 \n"
        : "=r"(ret)
        : "r"(msg)
        : "eax", "ebx"
    );
    return ret;
}

int sys_clear()
{
    int ret;
    __asm__ __volatile__(
        "mov $1, %%eax \n"
        "int $0x80 \n"
        "mov %%eax, %0 \n"
        : "=r"(ret)
        :
        : "eax"
    );
    return ret;
}
