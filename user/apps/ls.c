#include <stdint.h>
#include "kernel/syscall_api.h"

int user_main(int argc, char **argv)
{
    char buf[1024];
    const char *path = "/";
    if (argc >= 2 && argv[1] && argv[1][0] != '\0')
        path = argv[1];

    int n = sys_listdir(path, buf, (uint32_t)sizeof(buf));
    if (n < 0)
    {
        sys_write("ls: failed\n");
        return 1;
    }

    buf[(n < (int)sizeof(buf) - 1) ? n : ((int)sizeof(buf) - 1)] = '\0';
    sys_write(buf);
    return 0;
}
