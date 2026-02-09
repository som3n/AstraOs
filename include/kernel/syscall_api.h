#ifndef SYSCALL_API_H
#define SYSCALL_API_H

#include <stdint.h>
#include "kernel/syscall.h"

int sys_write(const char *msg);
int sys_clear();
__attribute__((noreturn)) void sys_exit(int code);

int sys_open(const char *path, uint32_t flags);
int sys_read(int fd, void *buf, uint32_t count);
int sys_writefd(int fd, const void *buf, uint32_t count);
int sys_close(int fd);
int sys_chdir(const char *path);
int sys_getcwd(char *buf, uint32_t size);
int sys_listdir(const char *path, char *out, uint32_t out_size);

#endif
