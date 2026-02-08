ENTRY(start)

SECTIONS
{
    . = 1M;
    kernel_start = .;

    .multiboot :
    {
        *(.multiboot)
    }

    . = ALIGN(4096);

    .text :
    {
        *(.text*)
    }

    . = ALIGN(4096);

    .rodata :
    {
        *(.rodata*)
    }

    . = ALIGN(4096);

    .data :
    {
        *(.data*)
    }

    . = ALIGN(4096);

    .bss :
    {
        *(.bss*)
        *(COMMON)
    }

    . = ALIGN(4096);

    kernel_end = .;
}
