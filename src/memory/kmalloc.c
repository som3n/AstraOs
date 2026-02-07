#include "memory/kmalloc.h"

static uint32_t heap_current = 0;

void kmalloc_init(uint32_t heap_start) {
    heap_current = heap_start;
}

void* kmalloc(uint32_t size) {

    // Align size to 4 bytes
    if (size % 4 != 0) {
        size = (size + 4) & ~3;
    }

    void* allocated = (void*)heap_current;
    heap_current += size;

    return allocated;
}
