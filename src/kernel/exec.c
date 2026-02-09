#include "kernel/exec.h"
#include "kernel/elf32.h"
#include "kernel/print.h"
#include "memory/paging.h"
#include "cpu/usermode.h"
#include "string.h"

#define USER_STACK_BASE 0x003FC000u
#define USER_STACK_TOP  0x00400000u

static uint32_t push_bytes(uint32_t sp, const void *src, uint32_t n)
{
    sp -= n;
    for (uint32_t i = 0; i < n; i++)
        ((uint8_t *)sp)[i] = ((const uint8_t *)src)[i];
    return sp;
}

static uint32_t push_u32(uint32_t sp, uint32_t v)
{
    return push_bytes(sp, &v, 4);
}

static uint32_t align_down(uint32_t sp, uint32_t align)
{
    return sp & ~(align - 1);
}

static uint32_t build_user_stack(int argc, const char *argv[], uint32_t stack_top)
{
    if (argc < 0)
        return 0;
    if (argc > 32)
        argc = 32;

    uint32_t sp = stack_top;

    // Copy argument strings near the top of stack (descending).
    uint32_t arg_ptrs[33];
    for (int i = argc - 1; i >= 0; i--)
    {
        const char *s = argv[i] ? argv[i] : "";
        uint32_t len = (uint32_t)strlen(s) + 1;

        if (len > 256)
            len = 256; // prevent insane args

        sp -= len;
        // Manual bounded copy.
        for (uint32_t j = 0; j < len; j++)
        {
            char c = s[j];
            ((char *)sp)[j] = c;
            if (c == '\0')
                break;
        }
        ((char *)sp)[len - 1] = '\0';
        arg_ptrs[i] = sp;
    }
    arg_ptrs[argc] = 0;

    // Align stack, then push argv pointers array, then argc.
    sp = align_down(sp, 16);

    // argv[argc] = NULL
    sp = push_u32(sp, 0);
    for (int i = argc - 1; i >= 0; i--)
        sp = push_u32(sp, arg_ptrs[i]);

    uint32_t argv_ptr = sp;
    sp = push_u32(sp, argv_ptr);
    sp = push_u32(sp, (uint32_t)argc);

    return sp;
}

int kernel_exec_elf_argv(const char *path, int argc, const char *argv[])
{
    uint32_t entry = 0;
    uint32_t low = 0;
    uint32_t high = 0;

    if (!elf32_load_from_fat16(path, &entry, &low, &high))
        return -1;

    // Remove user access from the whole user region and stack, then enable it only
    // for the new program's segments + stack.
    paging_clear_user(0x00200000u, 0x003F0000u);
    paging_clear_user(USER_STACK_BASE, USER_STACK_TOP);

    paging_mark_user(low, high);
    paging_mark_user(USER_STACK_BASE, USER_STACK_TOP);

    memset((void *)USER_STACK_BASE, 0, USER_STACK_TOP - USER_STACK_BASE);

    // Keep kernel supervisor-only (paging_init defaults to supervisor-only; this is extra safety).
    paging_protect_kernel();

    uint32_t user_sp = build_user_stack(argc, argv, USER_STACK_TOP);
    if (user_sp < USER_STACK_BASE || user_sp >= USER_STACK_TOP)
        return -1;

    return switch_to_user_mode(entry, user_sp);
}

int kernel_exec_elf(const char *path)
{
    const char *argv0[1];
    argv0[0] = path;
    return kernel_exec_elf_argv(path, 1, argv0);
}
