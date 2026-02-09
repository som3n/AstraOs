#ifndef IDT_H
#define IDT_H

#include <stdint.h>

void idt_init();
void idt_set_gate_flags(int n, uint32_t handler, uint8_t flags);
void idt_set_gate(int n, uint32_t handler);

#endif
