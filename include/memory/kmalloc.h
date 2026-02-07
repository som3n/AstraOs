#ifndef KMALLOC_H
#define KMALLOC_H

#include <stdint.h>

void kmalloc_init(uint32_t heap_start);
void* kmalloc(uint32_t size);

#endif
