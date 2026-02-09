#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>

void syscall_init();

enum
{
    SYS_WRITE = 0,
    SYS_CLEAR = 1,
    SYS_EXIT = 2,

    // Minimal filesystem / process syscalls
    SYS_OPEN = 3,
    SYS_READ = 4,
    SYS_CLOSE = 5,
    SYS_CHDIR = 6,
    SYS_GETCWD = 7,
    SYS_WRITEFD = 8,
    SYS_LISTDIR = 9
};

// open() flags (shared between kernel and user wrappers)
#define SYS_O_RDONLY 0
#define SYS_O_WRONLY (1u << 0)
#define SYS_O_APPEND (1u << 1)
#define SYS_O_CREAT  (1u << 2)
#define SYS_O_TRUNC  (1u << 3)

#endif
