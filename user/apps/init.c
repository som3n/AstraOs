#include <stdint.h>
#include "kernel/syscall_api.h"

int user_main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    sys_clear();
    sys_write("INIT.ELF: userland ready\n");

    // Create a small file so CAT.ELF has something to display.
    int fd = sys_open("/HELLO.TXT", SYS_O_WRONLY | SYS_O_CREAT | SYS_O_TRUNC);
    if (fd >= 0)
    {
        const char msg[] = "hello from /HELLO.TXT\n";
        sys_writefd(fd, msg, (uint32_t)(sizeof(msg) - 1));
        sys_close(fd);
    }

    return 0;
}
