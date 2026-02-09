#include <stdint.h>
#include "kernel/syscall_api.h"

int user_main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    char cwd[128];
    if (sys_getcwd(cwd, (uint32_t)sizeof(cwd)) < 0)
    {
        sys_write("pwd: failed\n");
        return 1;
    }
    sys_write(cwd);
    sys_write("\n");
    return 0;
}
