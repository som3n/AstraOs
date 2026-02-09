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
        first_page_table[i] = (i * PAGE_SIZE) | 7; // present + rw + user
    }

    // Link first page table into directory
    page_directory[0] = ((uint32_t)first_page_table) | 7;

    // Enable paging
    paging_enable((uint32_t)page_directory);
}
