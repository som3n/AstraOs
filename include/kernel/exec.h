#ifndef EXEC_H
#define EXEC_H

#include <stdint.h>

// Loads an ELF from the FAT16 disk image and runs it in ring3.
// Returns the user program exit code, or -1 on load/setup failure.
int kernel_exec_elf(const char *path);
int kernel_exec_elf_argv(const char *path, int argc, const char *argv[]);

#endif
