#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>

void paging_init();
void paging_enable(uint32_t page_directory);

#endif
