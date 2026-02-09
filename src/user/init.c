#include <stdint.h>
#include "kernel/syscall_api.h"

void user_init()
{
    sys_clear();
    sys_write("Hello from USER MODE!\n");

    char cwd[128];
    if (sys_getcwd(cwd, sizeof(cwd)) == 0)
    {
        sys_write("cwd: ");
        sys_write(cwd);
        sys_write("\n");
    }

    // Demo: create/write/read a file via FD syscalls.
    const char *path = "/HELLO.TXT";
    int fdw = sys_open(path, SYS_O_WRONLY | SYS_O_CREAT | SYS_O_TRUNC);
    if (fdw < 0)
    {
        sys_write("open(w) failed\n");
        sys_exit(0);
    }

    const char msg[] = "AstraOS usermode write works.\n";
    if (sys_writefd(fdw, msg, (uint32_t)(sizeof(msg) - 1)) < 0)
    {
        sys_write("writefd failed\n");
        sys_close(fdw);
        sys_exit(0);
    }
    sys_close(fdw);

    int fdr = sys_open(path, SYS_O_RDONLY);
    if (fdr < 0)
    {
        sys_write("open(r) failed\n");
        sys_exit(0);
    }

    char buf[96];
    int n = sys_read(fdr, buf, (uint32_t)(sizeof(buf) - 1));
    if (n < 0)
    {
        sys_write("read failed\n");
        sys_close(fdr);
        sys_exit(0);
    }
    buf[n] = '\0';
    sys_write("read back: ");
    sys_write(buf);
    sys_close(fdr);

    sys_exit(0);
}
