section .multiboot
align 4

dd 0x1BADB002
dd 0x0
dd -(0x1BADB002 + 0x0)

section .bss
align 16
stack_bottom:
    resb 16384          ; 16 KB stack
stack_top:

section .text
global start
extern kernel_main

start:
    cli
    mov esp, stack_top  ; setup stack

    call kernel_main

.hang:
    hlt
    jmp .hang
