#include "cpu/idt.h"

#define IDT_SIZE 256

typedef struct {
    uint16_t base_low;
    uint16_t selector;
    uint8_t  zero;
    uint8_t  flags;
    uint16_t base_high;
} __attribute__((packed)) idt_entry_t;

typedef struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) idt_ptr_t;

idt_entry_t idt[IDT_SIZE];
idt_ptr_t idt_ptr;

extern void idt_load(uint32_t);

void idt_set_gate(int n, uint32_t handler){
    idt[n].base_low = handler & 0xFFFF;
    idt[n].selector = 0x08;
    idt[n].zero = 0;
    idt[n].flags = 0x8E;
    idt[n].base_high = (handler >> 16) & 0xFFFF;
}

void idt_init() {
    idt_ptr.limit = (sizeof(idt_entry_t) * IDT_SIZE) - 1;
    idt_ptr.base = (uint32_t)&idt;

    for (int i = 0; i < IDT_SIZE; i++) {
        idt_set_gate(i, 0);
    }

    idt_load((uint32_t)&idt_ptr);
}
