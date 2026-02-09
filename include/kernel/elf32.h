#ifndef ELF32_H
#define ELF32_H

#include <stdint.h>

// Minimal ELF32 definitions for i386 ET_EXEC loaders.

#define EI_NIDENT 16

#define ELFMAG0 0x7F
#define ELFMAG1 'E'
#define ELFMAG2 'L'
#define ELFMAG3 'F'

#define ELFCLASS32 1
#define ELFDATA2LSB 1
#define EV_CURRENT 1

#define ET_EXEC 2
#define EM_386 3

#define PT_LOAD 1

typedef struct
{
    uint8_t e_ident[EI_NIDENT];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed)) elf32_ehdr_t;

typedef struct
{
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} __attribute__((packed)) elf32_phdr_t;

// Loads an ELF32 ET_EXEC from FAT16 into memory (identity-mapped).
// Returns 1 on success. On success, sets *out_entry to the entry virtual address,
// and *out_low/*out_high to the min/max virtual address range of loaded segments.
int elf32_load_from_fat16(const char *path, uint32_t *out_entry, uint32_t *out_low, uint32_t *out_high);

#endif

