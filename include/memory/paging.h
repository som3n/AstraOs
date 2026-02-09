#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>

void paging_init();
void paging_enable(uint32_t page_directory);
void paging_protect_kernel();
void paging_mark_user(uint32_t start, uint32_t end);
void paging_clear_user(uint32_t start, uint32_t end);

#endif
