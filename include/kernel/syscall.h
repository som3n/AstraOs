#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>

void syscall_init();

enum
{
    SYS_WRITE = 0,
    SYS_CLEAR = 1
};

#endif
