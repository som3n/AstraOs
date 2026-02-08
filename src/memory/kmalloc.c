#include "memory/kmalloc.h"
#include "vga.h"

#define HEAP_MAGIC 0xAABBCCDD

typedef struct heap_block
{
    uint32_t magic;
    uint32_t size;
    uint8_t free;
    struct heap_block *next;
} heap_block_t;

static heap_block_t *heap_head = 0;
static uint32_t heap_end_addr = 0;

static uint32_t align4(uint32_t size)
{
    return (size + 3) & ~3;
}

void kmalloc_init(uint32_t heap_start)
{
    heap_head = 0;
    heap_end_addr = heap_start;
}

static heap_block_t *find_free_block(uint32_t size)
{
    heap_block_t *current = heap_head;

    while (current)
    {
        if (current->magic != HEAP_MAGIC)
        {
            print("\n[HEAP ERROR] Heap corrupted!\n");
            while (1) { __asm__ __volatile__("cli; hlt"); }
        }

        if (current->free && current->size >= size)
            return current;

        current = current->next;
    }

    return 0;
}

static heap_block_t *extend_heap(uint32_t size)
{
    heap_block_t *new_block = (heap_block_t *)heap_end_addr;

    new_block->magic = HEAP_MAGIC;
    new_block->size = size;
    new_block->free = 0;
    new_block->next = 0;

    heap_end_addr += sizeof(heap_block_t) + size;

    // first allocation case
    if (!heap_head)
    {
        heap_head = new_block;
    }
    else
    {
        heap_block_t *current = heap_head;
        while (current->next)
            current = current->next;

        current->next = new_block;
    }

    return new_block;
}

void *kmalloc(uint32_t size)
{
    size = align4(size);

    heap_block_t *block = find_free_block(size);

    if (block)
    {
        block->free = 0;
        return (void *)((uint32_t)block + sizeof(heap_block_t));
    }

    block = extend_heap(size);
    return (void *)((uint32_t)block + sizeof(heap_block_t));
}

static void merge_free_blocks()
{
    heap_block_t *current = heap_head;

    while (current && current->next)
    {
        if (current->free && current->next->free)
        {
            current->size = current->size + sizeof(heap_block_t) + current->next->size;
            current->next = current->next->next;
        }
        else
        {
            current = current->next;
        }
    }
}

void kfree(void *ptr)
{
    if (!ptr)
        return;

    heap_block_t *block = (heap_block_t *)((uint32_t)ptr - sizeof(heap_block_t));

    if (block->magic != HEAP_MAGIC)
    {
        print("\n[HEAP ERROR] Invalid free detected!\n");
        return;
    }

    block->free = 1;
    merge_free_blocks();
}
