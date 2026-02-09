#include "cpu/gdt.h"
#include <stdint.h>

typedef struct
{
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_middle;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
} __attribute__((packed)) gdt_entry_t;

typedef struct
{
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) gdt_ptr_t;

/*
    GDT Layout:
    0 = Null
    1 = Kernel Code
    2 = Kernel Data
    3 = User Code
    4 = User Data
    5 = TSS
*/
static gdt_entry_t gdt_entries[6];
static gdt_ptr_t gdt_ptr;

extern void gdt_flush(uint32_t);

static void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran)
{
    gdt_entries[num].base_low = (base & 0xFFFF);
    gdt_entries[num].base_middle = (base >> 16) & 0xFF;
    gdt_entries[num].base_high = (base >> 24) & 0xFF;

    gdt_entries[num].limit_low = (limit & 0xFFFF);
    gdt_entries[num].granularity = (limit >> 16) & 0x0F;

    gdt_entries[num].granularity |= (gran & 0xF0);
    gdt_entries[num].access = access;
}

/* Public API used by TSS */
void gdt_set_tss(uint32_t base, uint32_t limit)
{
    // 32-bit available TSS descriptor:
    // access = 0x89 (P=1, DPL=0, S=0, Type=0x9)
    gdt_set_gate(5, base, limit, 0x89, 0x00);
}

void gdt_init()
{
    gdt_ptr.limit = (sizeof(gdt_entry_t) * 6) - 1;
    gdt_ptr.base = (uint32_t)&gdt_entries;

    // Null segment
    gdt_set_gate(0, 0, 0, 0, 0);

    // Kernel Code segment (ring 0)
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);

    // Kernel Data segment (ring 0)
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF);

    // User Code segment (ring 3)
    gdt_set_gate(3, 0, 0xFFFFFFFF, 0xFA, 0xCF);

    // User Data segment (ring 3)
    gdt_set_gate(4, 0, 0xFFFFFFFF, 0xF2, 0xCF);

    // Empty TSS slot (will be filled later)
    gdt_set_gate(5, 0, 0, 0, 0);

    gdt_flush((uint32_t)&gdt_ptr);
}
