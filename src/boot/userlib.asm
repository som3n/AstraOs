[bits 32]

global syscall_write
global syscall_clear

syscall_write:
    mov eax, 0          ; SYS_WRITE
    mov ebx, [esp+4]    ; msg pointer
    int 0x80
    ret

syscall_clear:
    mov eax, 1          ; SYS_CLEAR
    int 0x80
    ret
