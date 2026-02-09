#include "kernel/elf32.h"
#include "fs/fat16.h"
#include "kernel/print.h"
#include "string.h"

// Kernel is linked at 0x00100000, so keep user ET_EXEC images away from it.
#define USER_MIN_VADDR 0x00200000u
#define USER_MAX_VADDR 0x003F0000u

static int elf32_check_ident(const elf32_ehdr_t *eh)
{
    if (!eh)
        return 0;
    if (eh->e_ident[0] != ELFMAG0 ||
        eh->e_ident[1] != ELFMAG1 ||
        eh->e_ident[2] != ELFMAG2 ||
        eh->e_ident[3] != ELFMAG3)
        return 0;
    if (eh->e_ident[4] != ELFCLASS32)
        return 0;
    if (eh->e_ident[5] != ELFDATA2LSB)
        return 0;
    if (eh->e_ident[6] != EV_CURRENT)
        return 0;
    return 1;
}

int elf32_load_from_fat16(const char *path, uint32_t *out_entry, uint32_t *out_low, uint32_t *out_high)
{
    if (!path || !out_entry || !out_low || !out_high)
        return 0;

    *out_entry = 0;
    *out_low = 0;
    *out_high = 0;

    if (!fat16_init())
        return 0;

    uint32_t size = 0;
    if (!fat16_filesize(path, &size))
        return 0;

    if (size < sizeof(elf32_ehdr_t))
        return 0;

    // Read ELF header.
    elf32_ehdr_t eh;
    uint32_t rd = 0;
    if (!fat16_read_at(path, 0, (uint8_t *)&eh, sizeof(eh), &rd) || rd != sizeof(eh))
        return 0;

    if (!elf32_check_ident(&eh))
        return 0;

    if (eh.e_type != ET_EXEC || eh.e_machine != EM_386)
        return 0;

    if (eh.e_phentsize != sizeof(elf32_phdr_t) || eh.e_phnum == 0)
        return 0;

    uint32_t ph_table_bytes = (uint32_t)eh.e_phnum * (uint32_t)eh.e_phentsize;
    if (eh.e_phoff + ph_table_bytes > size)
        return 0;

    // Read program headers.
    elf32_phdr_t phdrs[32];
    if (eh.e_phnum > 32)
        return 0;

    rd = 0;
    if (!fat16_read_at(path, eh.e_phoff, (uint8_t *)phdrs, ph_table_bytes, &rd) || rd != ph_table_bytes)
        return 0;

    uint32_t low = 0xFFFFFFFFu;
    uint32_t high = 0;

    // Load PT_LOAD segments.
    for (uint32_t i = 0; i < eh.e_phnum; i++)
    {
        const elf32_phdr_t *ph = &phdrs[i];
        if (ph->p_type != PT_LOAD)
            continue;

        if (ph->p_memsz == 0)
            continue;

        if (ph->p_offset + ph->p_filesz > size)
            return 0;

        uint32_t seg_start = ph->p_vaddr;
        uint32_t seg_end = ph->p_vaddr + ph->p_memsz;

        if (seg_start < USER_MIN_VADDR || seg_end > USER_MAX_VADDR || seg_end <= seg_start)
            return 0;

        if (seg_start < low)
            low = seg_start;
        if (seg_end > high)
            high = seg_end;

        // Read file bytes directly into destination memory.
        if (ph->p_filesz > 0)
        {
            uint32_t got = 0;
            if (!fat16_read_at(path, ph->p_offset, (uint8_t *)ph->p_vaddr, ph->p_filesz, &got) || got != ph->p_filesz)
                return 0;
        }

        // Zero-fill BSS.
        if (ph->p_memsz > ph->p_filesz)
        {
            memset((void *)(ph->p_vaddr + ph->p_filesz), 0, ph->p_memsz - ph->p_filesz);
        }
    }

    if (low == 0xFFFFFFFFu || high == 0)
        return 0;

    // Entry must land within loaded region.
    if (eh.e_entry < low || eh.e_entry >= high)
        return 0;

    *out_entry = eh.e_entry;
    *out_low = low;
    *out_high = high;

    return 1;
}
