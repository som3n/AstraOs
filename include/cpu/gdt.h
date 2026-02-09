#ifndef GDT_H
#define GDT_H

#include <stdint.h>

void gdt_init();

/* Needed for TSS install */
void gdt_set_tss(uint32_t base, uint32_t limit);

#endif
