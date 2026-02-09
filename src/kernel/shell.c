#include "shell.h"
#include "vga.h"
#include "string.h"
#include "cpu/power.h"
#include "cpu/timer.h"
#include "keys.h"
#include "drivers/ata.h"
#include "memory/kmalloc.h"
#include "fs/fat16.h"
#include "kernel/print.h"
#include "kernel/syscall.h"
#include "kernel/syscall_api.h"

#define SHELL_BUFFER_SIZE 256
#define HISTORY_SIZE 10
#define MAX_ARGS 16

static char history[HISTORY_SIZE][SHELL_BUFFER_SIZE];
static int history_count = 0;
static int history_index = 0;

static char command_buffer[SHELL_BUFFER_SIZE];
static int cursor_pos = 0;
static int buffer_length = 0;

static int prompt_x = 0;
static int prompt_y = 0;

/* ---------- Prototypes ---------- */
static void history_add(const char *cmd);
static void load_command(const char *cmd);
static void redraw_command_line();
static void shell_prompt();

static int shell_tokenize(char *input, char *argv[], int max_args);
static void shell_execute(char *cmd);

/* ---------- Shell Prompt ---------- */

static void shell_prompt()
{
    print("\nAstraOS@");
    print(fat16_get_path());
    print("$ ");

    prompt_x = get_cursor_x();
    prompt_y = get_cursor_y();

    cursor_pos = 0;
    buffer_length = 0;
    command_buffer[0] = '\0';

    history_index = history_count;
}

/* ---------- Redraw Command Line ---------- */

static void redraw_command_line()
{
    // Clear current line from prompt to end
    for (int i = 0; i < VGA_WIDTH - prompt_x; i++)
    {
        put_char_at(' ', prompt_x + i, prompt_y);
    }

    // Draw command buffer
    for (int i = 0; i < buffer_length; i++)
    {
        put_char_at(command_buffer[i], prompt_x + i, prompt_y);
    }

    // Move cursor to correct position
    set_cursor_position(prompt_x + cursor_pos, prompt_y);
}

/* ---------- Tokenizer ---------- */

static int shell_tokenize(char *input, char *argv[], int max_args)
{
    int argc = 0;

    while (*input && argc < max_args)
    {
        // Skip leading spaces
        while (*input == ' ')
            input++;

        if (*input == '\0')
            break;

        argv[argc++] = input;

        // Move until next space
        while (*input && *input != ' ')
            input++;

        if (*input == ' ')
        {
            *input = '\0';
            input++;
        }
    }

    return argc;
}

/* ---------- Shell Execute ---------- */

static void shell_execute(char *cmd)
{
    char *argv[MAX_ARGS];
    int argc = shell_tokenize(cmd, argv, MAX_ARGS);

    if (argc == 0)
        return;

    char *command = argv[0];

    /* ==========================
       BASIC SYSTEM COMMANDS
       ========================== */

    if (strcmp(command, "help") == 0)
    {
        print("\nAvailable commands:\n\n");

        print("System:\n");
        print("  help              Show this help menu\n");
        print("  clear             Clear screen\n");
        print("  about             About AstraOS\n");
        print("  version           Show OS version\n");
        print("  uname             Kernel information\n");
        print("  uptime            Show system uptime\n");
        print("  sleep <sec>       Sleep for N seconds\n");
        print("  halt              Halt the CPU\n");
        print("  reboot            Reboot the system\n\n");

        print("Shell:\n");
        print("  history           Show command history\n");
        print("  echo <text>       Print text\n\n");

        print("Disk:\n");
        print("  diskread          Read disk sector 0 (test)\n");
        print("  disktest          Write + read test sector\n");
        print("  fatinfo           Show FAT16 boot sector info\n\n");

        print("Filesystem (FAT16):\n");
        print("  ls [path]         List directory\n");
        print("  pwd               Print working directory\n");
        print("  cd <path>         Change directory\n");
        print("  cat <file>        Display file contents\n");
        print("  touch <file>      Create empty file\n");
        print("  write             Write text to file \n");
        print("  append            Append text to file \n");
        print("  cp <src> <dst>    Copy file\n");
        print("  mv <src> <dst>    Move/Rename file\n");
        print("  mkdir <dir>       Create directory\n");
        print("  mkdir -p <path>   Create directory tree\n");
        print("  rm <file>         Delete file\n");
        print("  rm -r <path>      Delete file/folder recursively\n");
        print("  rmdir <dir>       Remove empty directory\n");
        print("  rmdir -r <path>   Remove directory recursively\n");

        return;
    }

    else if (strcmp(command, "clear") == 0)
    {
        clear_screen();
        return;
    }

    else if (strcmp(command, "about") == 0)
    {
        print("\nAstraOS - Custom kernel written from scratch.\n");
        print("Developer: Somen\n");
        return;
    }

    else if (strcmp(command, "version") == 0)
    {
        print("\nAstraOS version 0.1\n");
        return;
    }

    else if (strcmp(command, "uname") == 0)
    {
        print("\nAstraOS Kernel 0.1 i386\n");
        return;
    }

    else if (strcmp(command, "uptime") == 0)
    {
        uint32_t t = timer_get_ticks();
        uint32_t seconds = t / 100;

        print("\nUptime: ");
        print_uint(seconds);
        print(" seconds\n");
        return;
    }

    else if (strcmp(command, "sleep") == 0)
    {
        if (argc < 2)
        {
            print("\nUsage: sleep <seconds>\n");
            return;
        }

        uint32_t sec = 0;

        for (int i = 0; argv[1][i] != '\0'; i++)
        {
            if (argv[1][i] < '0' || argv[1][i] > '9')
            {
                print("\nInvalid number.\n");
                return;
            }
            sec = sec * 10 + (argv[1][i] - '0');
        }

        print("\nSleeping...\n");
        timer_sleep(sec);
        print("Done.\n");
        return;
    }

    else if (strcmp(command, "halt") == 0)
    {
        print("\nSystem halting...\n");
        cpu_halt();
        return;
    }

    else if (strcmp(command, "reboot") == 0)
    {
        print("\nSystem rebooting...\n");
        cpu_reboot();
        return;
    }

    /* ==========================
       SHELL COMMANDS
       ========================== */

    else if (strcmp(command, "echo") == 0)
    {
        if (argc < 2)
        {
            print("\n\n");
            return;
        }

        print("\n");
        for (int i = 1; i < argc; i++)
        {
            print(argv[i]);
            if (i != argc - 1)
                print(" ");
        }
        print("\n");
        return;
    }

    else if (strcmp(command, "history") == 0)
    {
        print("\nCommand History:\n");

        for (int i = 0; i < history_count; i++)
        {
            print("  ");
            print(history[i]);
            print("\n");
        }
        return;
    }

    /* ==========================
       DISK COMMANDS
       ========================== */

    else if (strcmp(command, "diskread") == 0)
    {
        uint8_t *buf = (uint8_t *)kmalloc(512);
        ata_read_sector(0, buf);

        print("\nDisk Sector 0 (first 64 bytes):\n");

        for (int i = 0; i < 64; i++)
        {
            uint8_t b = buf[i];

            char hex[3];
            char *digits = "0123456789ABCDEF";

            hex[0] = digits[(b >> 4) & 0xF];
            hex[1] = digits[b & 0xF];
            hex[2] = '\0';

            print(hex);
            print(" ");
        }

        print("\n");
        return;
    }

    else if (strcmp(command, "disktest") == 0)
    {
        uint8_t *buf = (uint8_t *)kmalloc(512);

        for (int i = 0; i < 512; i++)
            buf[i] = 0;

        buf[0] = 'A';
        buf[1] = 'S';
        buf[2] = 'T';
        buf[3] = 'R';
        buf[4] = 'A';

        ata_write_sector(10, buf);

        for (int i = 0; i < 512; i++)
            buf[i] = 0;

        ata_read_sector(10, buf);

        print("\nRead back: ");
        print_char(buf[0]);
        print_char(buf[1]);
        print_char(buf[2]);
        print_char(buf[3]);
        print_char(buf[4]);
        print("\n");
        return;
    }

    else if (strcmp(command, "syscalltest") == 0)
    {
        static const char msg[] = "\nHello from SYS_WRITE syscall!\n";

        __asm__ __volatile__(
            "mov $0, %%eax \n" // SYS_WRITE
            "mov %0, %%ebx \n"
            "int $0x80 \n"
            :
            : "r"(msg)
            : "eax", "ebx");

        return;
    }

    else if (strcmp(command, "fatinfo") == 0)
    {
        if (!fat16_init())
        {
            print("\nFAT16 init failed.\n");
            return;
        }

        fat16_bpb_t info = fat16_get_bpb();

        print("\nFAT16 Boot Sector Info:\n");

        print("Bytes/Sector: ");
        print_uint(info.bytes_per_sector);

        print("\nSectors/Cluster: ");
        print_uint(info.sectors_per_cluster);

        print("\nReserved Sectors: ");
        print_uint(info.reserved_sectors);

        print("\nFAT Count: ");
        print_uint(info.num_fats);

        print("\nRoot Entries: ");
        print_uint(info.root_entries);

        print("\nSectors/FAT: ");
        print_uint(info.sectors_per_fat);

        print("\nTotal Sectors (16): ");
        print_uint(info.total_sectors_16);

        print("\nTotal Sectors (32): ");
        print_uint(info.total_sectors_32);

        print("\n");
        return;
    }

    /* ==========================
       FILESYSTEM COMMANDS
       ========================== */

    else if (strcmp(command, "pwd") == 0)
    {
        fat16_pwd();
        return;
    }

    else if (strcmp(command, "ls") == 0)
    {
        if (!fat16_init())
        {
            print("\nFAT16 init failed.\n");
            return;
        }

        if (argc == 1)
        {
            fat16_ls();
            return;
        }

        if (!fat16_ls_path(argv[1]))
        {
            print("\nDirectory not found.\n");
        }
        return;
    }

    else if (strcmp(command, "cd") == 0)
    {
        if (argc < 2)
        {
            print("\nUsage: cd <path>\n");
            return;
        }

        if (!fat16_init())
        {
            print("\nFAT16 init failed.\n");
            return;
        }

        if (!fat16_cd_path(argv[1]))
        {
            print("\nDirectory not found.\n");
        }
        return;
    }

    else if (strcmp(command, "cat") == 0)
    {
        if (argc < 2)
        {
            print("\nUsage: cat <file>\n");
            return;
        }

        if (!fat16_init())
        {
            print("\nFAT16 init failed.\n");
            return;
        }

        if (!fat16_cat(argv[1]))
        {
            print("\nFile not found.\n");
        }
        return;
    }

    else if (strcmp(command, "touch") == 0)
    {
        if (argc < 2)
        {
            print("\nUsage: touch <file>\n");
            return;
        }

        if (!fat16_init())
        {
            print("\nFAT16 init failed.\n");
            return;
        }

        if (fat16_touch(argv[1]))
            print("\nFile created.\n");
        else
            print("\nTouch failed.\n");

        return;
    }

    else if (strcmp(command, "mkdir") == 0)
    {
        if (argc < 2)
        {
            print("\nUsage: mkdir [-p] <path>\n");
            return;
        }

        if (!fat16_init())
        {
            print("\nFAT16 init failed.\n");
            return;
        }

        if (strcmp(argv[1], "-p") == 0)
        {
            if (argc < 3)
            {
                print("\nUsage: mkdir -p <path>\n");
                return;
            }

            if (fat16_mkdir_p(argv[2]))
                print("\nDirectory tree created.\n");
            else
                print("\nmkdir -p failed.\n");

            return;
        }

        if (fat16_mkdir(argv[1]))
            print("\nDirectory created.\n");
        else
            print("\nmkdir failed.\n");

        return;
    }

    else if (strcmp(command, "rm") == 0)
    {
        if (argc < 2)
        {
            print("\nUsage: rm [-r] <file/dir>\n");
            return;
        }

        if (!fat16_init())
        {
            print("\nFAT16 init failed.\n");
            return;
        }

        if (strcmp(argv[1], "-r") == 0)
        {
            if (argc < 3)
            {
                print("\nUsage: rm -r <path>\n");
                return;
            }

            if (fat16_rm_rf(argv[2]))
                print("\nDeleted recursively.\n");
            else
                print("\nrm -r failed.\n");

            return;
        }

        int result = fat16_rm(argv[1]);

        if (result == 1)
            print("\nFile deleted.\n");
        else if (result == -1)
            print("\nrm: cannot remove directory. Use rm -r.\n");
        else
            print("\nrm failed.\n");

        return;
    }

    else if (strcmp(command, "rmdir") == 0)
    {
        if (argc < 2)
        {
            print("\nUsage: rmdir [-r] <dirname>\n");
            return;
        }

        if (!fat16_init())
        {
            print("\nFAT16 init failed.\n");
            return;
        }

        if (strcmp(argv[1], "-r") == 0)
        {
            if (argc < 3)
            {
                print("\nUsage: rmdir -r <path>\n");
                return;
            }

            if (fat16_rm_rf(argv[2]))
                print("\nDirectory removed recursively.\n");
            else
                print("\nrmdir -r failed.\n");

            return;
        }

        int result = fat16_rmdir(argv[1]);

        if (result == 1)
            print("\nDirectory removed.\n");
        else if (result == -1)
            print("\nrmdir: not a directory.\n");
        else if (result == -2)
            print("\nrmdir: directory not empty.\n");
        else
            print("\nrmdir failed.\n");

        return;
    }

    else if (strcmp(command, "write") == 0)
    {
        if (argc < 3)
        {
            print("\nUsage: write <file> <text>\n");
            return;
        }

        char *filename = argv[1];

        // build full text from argv[2] onwards
        char text[256];
        text[0] = '\0';

        for (int i = 2; i < argc; i++)
        {
            strcat(text, argv[i]);
            if (i != argc - 1)
                strcat(text, " ");
        }

        if (!fat16_init())
        {
            print("\nFAT16 init failed.\n");
            return;
        }

        if (fat16_write_file(filename, (uint8_t *)text, strlen(text)))
            print("\nFile written.\n");
        else
            print("\nWrite failed.\n");

        return;
    }

    else if (strcmp(command, "append") == 0)
    {
        if (argc < 3)
        {
            print("\nUsage: append <file> <text>\n");
            return;
        }

        char *filename = argv[1];

        char text[256];
        text[0] = '\0';

        for (int i = 2; i < argc; i++)
        {
            strcat(text, argv[i]);
            if (i != argc - 1)
                strcat(text, " ");
        }

        if (!fat16_init())
        {
            print("\nFAT16 init failed.\n");
            return;
        }

        if (fat16_append_file(filename, (uint8_t *)text, strlen(text)))
            print("\nAppended.\n");
        else
            print("\nAppend failed.\n");

        return;
    }

    else if (strcmp(command, "cp") == 0)
    {
        if (argc < 3)
        {
            print("\nUsage: cp <src> <dst>\n");
            return;
        }

        if (!fat16_init())
        {
            print("\nFAT16 init failed.\n");
            return;
        }

        if (fat16_cp(argv[1], argv[2]))
            print("\nCopied.\n");
        else
            print("\ncp failed.\n");

        return;
    }

    else if (strcmp(command, "mv") == 0)
    {
        if (argc < 3)
        {
            print("\nUsage: mv <src> <dst>\n");
            return;
        }

        if (!fat16_init())
        {
            print("\nFAT16 init failed.\n");
            return;
        }

        if (fat16_mv(argv[1], argv[2]))
            print("\nMoved.\n");
        else
            print("\nmv failed.\n");

        return;
    }

    /* ==========================
       MEMORY TEST COMMANDS
       ========================== */

    else if (strcmp(command, "heaptest") == 0)
    {
        print("\nTesting heap...\n");

        char *a = (char *)kmalloc(32);
        char *b = (char *)kmalloc(64);

        a[0] = 'A';
        b[0] = 'B';

        print("Allocated A and B\n");

        kfree(a);
        kfree(b);

        print("Freed A and B\n");

        return;
    }

    /* ==========================
       UNKNOWN COMMAND
       ========================== */

    else
    {
        print("\nUnknown command: ");
        print(command);
        print("\nType 'help' for commands.\n");
    }
}

/* ---------- Shell Init ---------- */

void shell_init()
{
    print("Welcome to AstraOS Shell\n");
    print("Type 'help' to see available commands.\n");
    shell_prompt();
}

/* ---------- Keyboard Input Handler ---------- */

void shell_handle_input(int key)
{
    // UP ARROW (history previous)
    if (key == KEY_ARROW_UP)
    {
        if (history_count == 0)
            return;

        if (history_index > 0)
        {
            history_index--;
            load_command(history[history_index]);
        }
        return;
    }

    // DOWN ARROW (history next)
    if (key == KEY_ARROW_DOWN)
    {
        if (history_count == 0)
            return;

        if (history_index < history_count - 1)
        {
            history_index++;
            load_command(history[history_index]);
        }
        else
        {
            history_index = history_count;
            load_command("");
        }
        return;
    }

    // LEFT ARROW
    if (key == KEY_ARROW_LEFT)
    {
        if (cursor_pos > 0)
        {
            cursor_pos--;
            redraw_command_line();
        }
        return;
    }

    // RIGHT ARROW
    if (key == KEY_ARROW_RIGHT)
    {
        if (cursor_pos < buffer_length)
        {
            cursor_pos++;
            redraw_command_line();
        }
        return;
    }

    // DELETE KEY
    if (key == KEY_DELETE)
    {
        if (cursor_pos < buffer_length)
        {
            for (int i = cursor_pos; i < buffer_length - 1; i++)
            {
                command_buffer[i] = command_buffer[i + 1];
            }

            buffer_length--;
            command_buffer[buffer_length] = '\0';
            redraw_command_line();
        }
        return;
    }

    char c = (char)key;

    // ENTER
    if (c == '\n')
    {
        command_buffer[buffer_length] = '\0';

        history_add(command_buffer);

        print("\n");
        shell_execute(command_buffer);

        shell_prompt();
        return;
    }

    // BACKSPACE
    if (c == '\b')
    {
        if (cursor_pos > 0)
        {
            for (int i = cursor_pos - 1; i < buffer_length - 1; i++)
            {
                command_buffer[i] = command_buffer[i + 1];
            }

            cursor_pos--;
            buffer_length--;
            command_buffer[buffer_length] = '\0';

            redraw_command_line();
        }
        return;
    }

    // Ignore non-printable characters
    if (c < 32 || c > 126)
    {
        return;
    }

    // Buffer full
    if (buffer_length >= SHELL_BUFFER_SIZE - 1)
    {
        return;
    }

    // Insert mode typing (shift right)
    for (int i = buffer_length; i > cursor_pos; i--)
    {
        command_buffer[i] = command_buffer[i - 1];
    }

    command_buffer[cursor_pos] = c;
    buffer_length++;
    cursor_pos++;

    command_buffer[buffer_length] = '\0';

    redraw_command_line();
}

/* ---------- History ---------- */

static void history_add(const char *cmd)
{
    if (cmd[0] == '\0')
        return;

    // If full, shift old commands
    if (history_count >= HISTORY_SIZE)
    {
        for (int i = 1; i < HISTORY_SIZE; i++)
        {
            strcpy(history[i - 1], history[i]);
        }
        history_count = HISTORY_SIZE - 1;
    }

    strcpy(history[history_count], cmd);
    history_count++;

    history_index = history_count;
}

static void load_command(const char *cmd)
{
    int len = strlen(cmd);

    if (len >= SHELL_BUFFER_SIZE)
        len = SHELL_BUFFER_SIZE - 1;

    for (int i = 0; i < len; i++)
    {
        command_buffer[i] = cmd[i];
    }

    command_buffer[len] = '\0';

    buffer_length = len;
    cursor_pos = len;

    redraw_command_line();
}
