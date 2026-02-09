[bits 32]

global _start
extern user_main

_start:
    ; Stack ABI from kernel:
    ;   [esp+0] argc
    ;   [esp+4] argv (char**)
    mov eax, [esp]
    mov ebx, [esp+4]
    push ebx
    push eax
    call user_main
    add esp, 8
    ; If user_main returns, exit with code in EAX.
    mov ecx, eax
    mov eax, 2      ; SYS_EXIT
    mov ebx, ecx
    int 0x80
.hang:
    pause
    jmp .hang
