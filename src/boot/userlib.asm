[bits 32]

global syscall_write
global syscall_clear
global syscall_exit

syscall_write:
    mov eax, 0          ; SYS_WRITE
    mov ebx, [esp+4]    ; msg pointer
    int 0x80
    ret

syscall_clear:
    mov eax, 1          ; SYS_CLEAR
    int 0x80
    ret

syscall_exit:
    mov eax, 2          ; SYS_EXIT
    mov ebx, [esp+4]    ; exit code
    int 0x80
.hang:
    pause
    jmp .hang
