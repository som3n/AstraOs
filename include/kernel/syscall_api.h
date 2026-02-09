#ifndef SYSCALL_API_H
#define SYSCALL_API_H

#include <stdint.h>

int sys_write(const char *msg);
int sys_clear();

#endif
