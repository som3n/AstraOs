#include "kernel/syscall_api.h"

int user_main(int argc, char **argv)
{
    if (argc <= 1)
    {
        sys_write("\n");
        return 0;
    }

    for (int i = 1; i < argc; i++)
    {
        if (argv[i])
            sys_write(argv[i]);
        if (i != argc - 1)
            sys_write(" ");
    }
    sys_write("\n");
    return 0;
}
