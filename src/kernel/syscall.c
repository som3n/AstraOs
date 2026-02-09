#include "kernel/syscall.h"
#include "cpu/isr.h"
#include "kernel/print.h"
#include "vga.h"
#include "cpu/usermode.h"
#include "fs/fat16.h"

#define MAX_FDS 16
#define FD_PATH_MAX 128

typedef struct
{
    int used;
    uint32_t flags;
    uint32_t offset;
    uint32_t size;
    char path[FD_PATH_MAX];
} fd_entry_t;

static fd_entry_t fd_table[MAX_FDS];

static int copy_cstr_bounded(char *dst, const char *src, int dst_size)
{
    if (!dst || !src || dst_size <= 0)
        return 0;

    int i = 0;
    for (; i < dst_size - 1; i++)
    {
        char c = src[i];
        dst[i] = c;
        if (c == '\0')
            return 1;
    }
    dst[i] = '\0';
    return 1;
}

static int fd_alloc()
{
    for (int i = 0; i < MAX_FDS; i++)
    {
        if (!fd_table[i].used)
        {
            fd_table[i].used = 1;
            fd_table[i].flags = 0;
            fd_table[i].offset = 0;
            fd_table[i].size = 0;
            fd_table[i].path[0] = '\0';
            return i;
        }
    }
    return -1;
}

static void fd_free(int fd)
{
    if (fd < 0 || fd >= MAX_FDS)
        return;
    fd_table[fd].used = 0;
    fd_table[fd].flags = 0;
    fd_table[fd].offset = 0;
    fd_table[fd].size = 0;
    fd_table[fd].path[0] = '\0';
}

static void syscall_handler(registers_t *r)
{
    uint32_t syscall_num = r->eax;

    if (syscall_num == SYS_WRITE)
    {
        char *msg = (char *)r->ebx;
        print(msg);
        r->eax = 0; // return success
    }
    else if (syscall_num == SYS_CLEAR)
    {
        clear_screen();
        r->eax = 0;
    }
    else if (syscall_num == SYS_EXIT)
    {
        // Never returns: switches back to the kernel resume point.
        usermode_exit((int)r->ebx);
    }
    else if (syscall_num == SYS_OPEN)
    {
        const char *path = (const char *)r->ebx;
        uint32_t flags = r->ecx;
        if (!path)
        {
            r->eax = (uint32_t)-1;
            return;
        }

        if (!fat16_init())
        {
            r->eax = (uint32_t)-1;
            return;
        }

        uint32_t fsize = 0;
        int exists = fat16_filesize(path, &fsize);

        if (!exists)
        {
            if (flags & SYS_O_CREAT)
            {
                // Create empty file (write 0 bytes).
                if (!fat16_write_file(path, (const uint8_t *)"", 0))
                {
                    r->eax = (uint32_t)-1;
                    return;
                }
                fsize = 0;
            }
            else
            {
                r->eax = (uint32_t)-1;
                return;
            }
        }

        int fd = fd_alloc();
        if (fd < 0)
        {
            r->eax = (uint32_t)-1;
            return;
        }

        copy_cstr_bounded(fd_table[fd].path, path, FD_PATH_MAX);
        fd_table[fd].flags = flags;
        fd_table[fd].size = fsize;

        if (flags & SYS_O_APPEND)
            fd_table[fd].offset = fsize;
        else
            fd_table[fd].offset = 0;

        r->eax = (uint32_t)fd;
    }
    else if (syscall_num == SYS_READ)
    {
        int fd = (int)r->ebx;
        uint8_t *buf = (uint8_t *)r->ecx;
        uint32_t count = r->edx;

        if (fd < 0 || fd >= MAX_FDS || !fd_table[fd].used || !buf)
        {
            r->eax = (uint32_t)-1;
            return;
        }

        if (fd_table[fd].flags & SYS_O_WRONLY)
        {
            r->eax = (uint32_t)-1;
            return;
        }

        if (!fat16_init())
        {
            r->eax = (uint32_t)-1;
            return;
        }

        uint32_t out_read = 0;
        if (!fat16_read_at(fd_table[fd].path, fd_table[fd].offset, buf, count, &out_read))
        {
            r->eax = (uint32_t)-1;
            return;
        }

        fd_table[fd].offset += out_read;
        r->eax = out_read;
    }
    else if (syscall_num == SYS_WRITEFD)
    {
        int fd = (int)r->ebx;
        const uint8_t *buf = (const uint8_t *)r->ecx;
        uint32_t count = r->edx;

        if (fd < 0 || fd >= MAX_FDS || !fd_table[fd].used || !buf)
        {
            r->eax = (uint32_t)-1;
            return;
        }

        if (!(fd_table[fd].flags & SYS_O_WRONLY))
        {
            r->eax = (uint32_t)-1;
            return;
        }

        if (!fat16_init())
        {
            r->eax = (uint32_t)-1;
            return;
        }

        int ok = 0;

        if (fd_table[fd].flags & SYS_O_APPEND)
        {
            ok = fat16_append_file(fd_table[fd].path, buf, count);
        }
        else if ((fd_table[fd].flags & SYS_O_TRUNC) && fd_table[fd].offset == 0)
        {
            // First write after open(trunc) rewrites file. Further writes append.
            ok = fat16_write_file(fd_table[fd].path, buf, count);
            fd_table[fd].flags &= ~SYS_O_TRUNC;
        }
        else
        {
            ok = fat16_append_file(fd_table[fd].path, buf, count);
        }

        if (!ok)
        {
            r->eax = (uint32_t)-1;
            return;
        }

        fd_table[fd].offset += count;
        fd_table[fd].size += count;
        r->eax = count;
    }
    else if (syscall_num == SYS_CLOSE)
    {
        int fd = (int)r->ebx;
        if (fd < 0 || fd >= MAX_FDS || !fd_table[fd].used)
        {
            r->eax = (uint32_t)-1;
            return;
        }
        fd_free(fd);
        r->eax = 0;
    }
    else if (syscall_num == SYS_CHDIR)
    {
        const char *path = (const char *)r->ebx;
        if (!path)
        {
            r->eax = (uint32_t)-1;
            return;
        }
        if (!fat16_init())
        {
            r->eax = (uint32_t)-1;
            return;
        }
        r->eax = fat16_cd_path(path) ? 0 : (uint32_t)-1;
    }
    else if (syscall_num == SYS_GETCWD)
    {
        char *out = (char *)r->ebx;
        uint32_t size = r->ecx;
        if (!out || size == 0)
        {
            r->eax = (uint32_t)-1;
            return;
        }

        const char *cwd = fat16_get_path();
        if (!cwd)
        {
            r->eax = (uint32_t)-1;
            return;
        }

        // Copy out at most size-1 bytes and NUL terminate.
        uint32_t i = 0;
        while (i + 1 < size)
        {
            char c = cwd[i];
            out[i] = c;
            if (c == '\0')
                break;
            i++;
        }
        if (i + 1 >= size)
            out[size - 1] = '\0';

        r->eax = 0;
    }
    else if (syscall_num == SYS_LISTDIR)
    {
        const char *path = (const char *)r->ebx;
        char *out = (char *)r->ecx;
        uint32_t out_size = r->edx;

        if (!path || !out || out_size == 0)
        {
            r->eax = (uint32_t)-1;
            return;
        }

        if (!fat16_init())
        {
            r->eax = (uint32_t)-1;
            return;
        }

        uint32_t written = 0;
        if (!fat16_list_dir(path, out, out_size, &written))
        {
            r->eax = (uint32_t)-1;
            return;
        }

        r->eax = written;
    }
    else
    {
        print("\n[SYSCALL] Unknown syscall\n");
        r->eax = (uint32_t)-1;
    }
}

void syscall_init()
{
    isr_register_handler(0x80, syscall_handler);
    print("\nSyscall system ready (int 0x80)\n");
}
