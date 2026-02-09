#include "cpu/isr.h"
#include "cpu/idt.h"
#include "kernel/print.h"
#include "vga.h"

static isr_t interrupt_handlers[256];

extern void isr0();
extern void isr1();
extern void isr2();
extern void isr3();
extern void isr4();
extern void isr5();
extern void isr6();
extern void isr7();
extern void isr8();
extern void isr9();
extern void isr10();
extern void isr11();
extern void isr12();
extern void isr13();
extern void isr14();
extern void isr15();
extern void isr16();
extern void isr17();
extern void isr18();
extern void isr19();
extern void isr20();
extern void isr21();
extern void isr22();
extern void isr23();
extern void isr24();
extern void isr25();
extern void isr26();
extern void isr27();
extern void isr28();
extern void isr29();
extern void isr30();
extern void isr31();

static const char *exception_messages[] = {
    "Division By Zero",
    "Debug",
    "Non Maskable Interrupt",
    "Breakpoint",
    "Into Detected Overflow",
    "Out of Bounds",
    "Invalid Opcode",
    "No Coprocessor",

    "Double Fault",
    "Coprocessor Segment Overrun",
    "Bad TSS",
    "Segment Not Present",
    "Stack Fault",
    "General Protection Fault",
    "Page Fault",
    "Unknown Interrupt",

    "Coprocessor Fault",
    "Alignment Check",
    "Machine Check",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",

    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved"};

void enable_interrupts()
{
    __asm__ __volatile__("sti");
}

static uint32_t read_cr2()
{
    uint32_t cr2;
    __asm__ __volatile__("mov %%cr2, %0" : "=r"(cr2));
    return cr2;
}

void isr_install()
{
    // clear handler table
    for (int i = 0; i < 256; i++)
        interrupt_handlers[i] = 0;

    idt_set_gate(0, (uint32_t)isr0);
    idt_set_gate(1, (uint32_t)isr1);
    idt_set_gate(2, (uint32_t)isr2);
    idt_set_gate(3, (uint32_t)isr3);
    idt_set_gate(4, (uint32_t)isr4);
    idt_set_gate(5, (uint32_t)isr5);
    idt_set_gate(6, (uint32_t)isr6);
    idt_set_gate(7, (uint32_t)isr7);
    idt_set_gate(8, (uint32_t)isr8);
    idt_set_gate(9, (uint32_t)isr9);
    idt_set_gate(10, (uint32_t)isr10);
    idt_set_gate(11, (uint32_t)isr11);
    idt_set_gate(12, (uint32_t)isr12);
    idt_set_gate(13, (uint32_t)isr13);
    idt_set_gate(14, (uint32_t)isr14);
    idt_set_gate(15, (uint32_t)isr15);
    idt_set_gate(16, (uint32_t)isr16);
    idt_set_gate(17, (uint32_t)isr17);
    idt_set_gate(18, (uint32_t)isr18);
    idt_set_gate(19, (uint32_t)isr19);
    idt_set_gate(20, (uint32_t)isr20);
    idt_set_gate(21, (uint32_t)isr21);
    idt_set_gate(22, (uint32_t)isr22);
    idt_set_gate(23, (uint32_t)isr23);
    idt_set_gate(24, (uint32_t)isr24);
    idt_set_gate(25, (uint32_t)isr25);
    idt_set_gate(26, (uint32_t)isr26);
    idt_set_gate(27, (uint32_t)isr27);
    idt_set_gate(28, (uint32_t)isr28);
    idt_set_gate(29, (uint32_t)isr29);
    idt_set_gate(30, (uint32_t)isr30);
    idt_set_gate(31, (uint32_t)isr31);

    extern void isr128();
    idt_set_gate(128, (uint32_t)isr128);
}

void isr_register_handler(uint8_t n, isr_t handler)
{
    interrupt_handlers[n] = handler;
}

void isr_handler(registers_t *r)
{
    // If a custom handler exists (like syscall int 0x80), run it
    if (interrupt_handlers[r->int_no] != 0)
    {
        isr_t handler = interrupt_handlers[r->int_no];
        handler(r);
        return;
    }

    // Default exception handling
    print("\n\n[EXCEPTION] ");

    if (r->int_no < 32)
        print(exception_messages[r->int_no]);
    else
        print("Unknown Interrupt");

    print("\nInterrupt: ");
    print_uint(r->int_no);

    print("\nError Code: ");
    print_uint(r->err_code);

    // Page Fault extra debugging
    if (r->int_no == 14)
    {
        uint32_t fault_addr = read_cr2();

        print("\nFault Address: ");
        print_uint(fault_addr);

        print("\nReason: ");

        if (!(r->err_code & 0x1))
            print("Page not present ");

        if (r->err_code & 0x2)
            print("Write ");
        else
            print("Read ");

        if (r->err_code & 0x4)
            print("User-mode ");
        else
            print("Kernel-mode ");

        if (r->err_code & 0x8)
            print("Reserved-bit violation ");

        if (r->err_code & 0x10)
            print("Instruction fetch ");
    }

    print("\nSystem Halted.\n");

    while (1)
    {
        __asm__ __volatile__("cli; hlt");
    }
}
