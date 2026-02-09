#include <stdint.h>
#include "kernel/syscall_api.h"

int user_main(int argc, char **argv)
{
    const char *path = "/HELLO.TXT";
    if (argc >= 2 && argv[1] && argv[1][0] != '\0')
        path = argv[1];

    int fd = sys_open(path, SYS_O_RDONLY);
    if (fd < 0)
    {
        sys_write("cat: open failed\n");
        return 1;
    }

    char buf[256];
    for (;;)
    {
        int n = sys_read(fd, buf, (uint32_t)sizeof(buf) - 1);
        if (n < 0)
        {
            sys_write("cat: read failed\n");
            sys_close(fd);
            return 1;
        }
        if (n == 0)
            break;
        buf[n] = '\0';
        sys_write(buf);
    }

    sys_close(fd);
    return 0;
}
