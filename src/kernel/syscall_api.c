#include "kernel/syscall_api.h"

int sys_write(const char *msg)
{
    int ret;
    __asm__ __volatile__(
        "mov $0, %%eax \n"
        "mov %1, %%ebx \n"
        "int $0x80 \n"
        "mov %%eax, %0 \n"
        : "=r"(ret)
        : "r"(msg)
        : "eax", "ebx"
    );
    return ret;
}

int sys_clear()
{
    int ret;
    __asm__ __volatile__(
        "mov $1, %%eax \n"
        "int $0x80 \n"
        "mov %%eax, %0 \n"
        : "=r"(ret)
        :
        : "eax"
    );
    return ret;
}

void sys_exit(int code)
{
    __asm__ __volatile__(
        "mov $2, %%eax \n"
        "mov %0, %%ebx \n"
        "int $0x80 \n"
        :
        : "r"(code)
        : "eax", "ebx"
    );

    // If the kernel doesn't honor SYS_EXIT for some reason, don't fall through.
    for (;;)
        __asm__ __volatile__("pause");
}

int sys_open(const char *path, uint32_t flags)
{
    int ret;
    __asm__ __volatile__(
        "mov $3, %%eax \n"
        "mov %1, %%ebx \n"
        "mov %2, %%ecx \n"
        "int $0x80 \n"
        "mov %%eax, %0 \n"
        : "=r"(ret)
        : "r"(path), "r"(flags)
        : "eax", "ebx", "ecx"
    );
    return ret;
}

int sys_read(int fd, void *buf, uint32_t count)
{
    int ret;
    __asm__ __volatile__(
        "mov $4, %%eax \n"
        "mov %1, %%ebx \n"
        "mov %2, %%ecx \n"
        "mov %3, %%edx \n"
        "int $0x80 \n"
        "mov %%eax, %0 \n"
        : "=r"(ret)
        : "r"(fd), "r"(buf), "r"(count)
        : "eax", "ebx", "ecx", "edx"
    );
    return ret;
}

int sys_writefd(int fd, const void *buf, uint32_t count)
{
    int ret;
    __asm__ __volatile__(
        "mov $8, %%eax \n"
        "mov %1, %%ebx \n"
        "mov %2, %%ecx \n"
        "mov %3, %%edx \n"
        "int $0x80 \n"
        "mov %%eax, %0 \n"
        : "=r"(ret)
        : "r"(fd), "r"(buf), "r"(count)
        : "eax", "ebx", "ecx", "edx"
    );
    return ret;
}

int sys_close(int fd)
{
    int ret;
    __asm__ __volatile__(
        "mov $5, %%eax \n"
        "mov %1, %%ebx \n"
        "int $0x80 \n"
        "mov %%eax, %0 \n"
        : "=r"(ret)
        : "r"(fd)
        : "eax", "ebx"
    );
    return ret;
}

int sys_chdir(const char *path)
{
    int ret;
    __asm__ __volatile__(
        "mov $6, %%eax \n"
        "mov %1, %%ebx \n"
        "int $0x80 \n"
        "mov %%eax, %0 \n"
        : "=r"(ret)
        : "r"(path)
        : "eax", "ebx"
    );
    return ret;
}

int sys_getcwd(char *buf, uint32_t size)
{
    int ret;
    __asm__ __volatile__(
        "mov $7, %%eax \n"
        "mov %1, %%ebx \n"
        "mov %2, %%ecx \n"
        "int $0x80 \n"
        "mov %%eax, %0 \n"
        : "=r"(ret)
        : "r"(buf), "r"(size)
        : "eax", "ebx", "ecx"
    );
    return ret;
}

int sys_listdir(const char *path, char *out, uint32_t out_size)
{
    int ret;
    __asm__ __volatile__(
        "mov $9, %%eax \n"
        "mov %1, %%ebx \n"
        "mov %2, %%ecx \n"
        "mov %3, %%edx \n"
        "int $0x80 \n"
        "mov %%eax, %0 \n"
        : "=r"(ret)
        : "r"(path), "r"(out), "r"(out_size)
        : "eax", "ebx", "ecx", "edx"
    );
    return ret;
}
