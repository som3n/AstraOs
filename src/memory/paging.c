#include "memory/paging.h"
#include "kernel/print.h"
#include "vga.h"

#define PAGE_SIZE 4096

// Page directory + page table (aligned)
static uint32_t page_directory[1024] __attribute__((aligned(4096)));
static uint32_t first_page_table[1024] __attribute__((aligned(4096)));

void paging_enable(uint32_t page_directory_addr)
{
    __asm__ __volatile__("mov %0, %%cr3" : : "r"(page_directory_addr));

    uint32_t cr0;
    __asm__ __volatile__("mov %%cr0, %0" : "=r"(cr0));

    cr0 |= 0x80000000; // Set PG bit (paging enable)

    __asm__ __volatile__("mov %0, %%cr0" : : "r"(cr0));
}

static void paging_flush()
{
    __asm__ __volatile__("mov %0, %%cr3" : : "r"((uint32_t)page_directory) : "memory");
}

void paging_init()
{
    // Clear page directory
    for (int i = 0; i < 1024; i++)
    {
        page_directory[i] = 0x00000002; // supervisor, read/write, not present
    }

    // Fill first page table (identity map first 4MB)
    for (int i = 0; i < 1024; i++)
    {
        // Default to supervisor-only; user mappings will be enabled per page.
        first_page_table[i] = (i * PAGE_SIZE) | 3; // present + rw
    }

    // Link first page table into directory
    // PDE must be user-accessible for ring3 to reach user PTEs; kernel pages remain supervisor via PTEs.
    page_directory[0] = ((uint32_t)first_page_table) | 7;

    // Enable paging
    paging_enable((uint32_t)page_directory);
}

void paging_protect_kernel()
{
    extern uint32_t kernel_start;
    extern uint32_t kernel_end;

    uint32_t start = (uint32_t)&kernel_start;
    uint32_t end = (uint32_t)&kernel_end;

    if (end < start)
        return;

    // This paging setup identity-maps only the first 4MB.
    if (start >= 0x400000)
        return;
    if (end > 0x400000)
        end = 0x400000;

    start &= 0xFFFFF000;
    end = (end + PAGE_SIZE - 1) & 0xFFFFF000;

    for (uint32_t addr = start; addr < end; addr += PAGE_SIZE)
    {
        uint32_t idx = addr / PAGE_SIZE;
        // Clear user bit (bit 2), keep present/rw as-is.
        first_page_table[idx] &= ~0x4;
    }

    paging_flush();
}

void paging_mark_user(uint32_t start, uint32_t end)
{
    if (end < start)
        return;

    if (start >= 0x400000)
        return;
    if (end > 0x400000)
        end = 0x400000;

    start &= 0xFFFFF000;
    end = (end + PAGE_SIZE - 1) & 0xFFFFF000;

    for (uint32_t addr = start; addr < end; addr += PAGE_SIZE)
    {
        uint32_t idx = addr / PAGE_SIZE;
        first_page_table[idx] |= 0x4; // user bit
    }

    paging_flush();
}

void paging_clear_user(uint32_t start, uint32_t end)
{
    if (end < start)
        return;

    if (start >= 0x400000)
        return;
    if (end > 0x400000)
        end = 0x400000;

    start &= 0xFFFFF000;
    end = (end + PAGE_SIZE - 1) & 0xFFFFF000;

    for (uint32_t addr = start; addr < end; addr += PAGE_SIZE)
    {
        uint32_t idx = addr / PAGE_SIZE;
        first_page_table[idx] &= ~0x4;
    }

    paging_flush();
}
