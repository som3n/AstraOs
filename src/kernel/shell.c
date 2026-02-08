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

#define SHELL_BUFFER_SIZE 256
#define HISTORY_SIZE 10

static char history[HISTORY_SIZE][SHELL_BUFFER_SIZE];
static int history_count = 0;
static int history_index = 0;

static char command_buffer[SHELL_BUFFER_SIZE];
static int cursor_pos = 0;
static int buffer_length = 0;

static int prompt_x = 0;
static int prompt_y = 0;

// function prototypes
static void history_add(const char *cmd);
static void load_command(const char *cmd);

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

static void shell_execute(char *cmd)
{

    char *args = 0;
    for (int i = 0; cmd[i] != '\0'; i++)
    {
        if (cmd[i] == ' ')
        {
            cmd[i] = '\0';
            args = &cmd[i + 1];
            break;
        }
    }

    if (strcmp(cmd, "help") == 0)
    {
        print("\nAvailable commands:\n");
        print("help     - Show this help menu\n");
        print("clear    - Clear screen\n");
        print("about    - About AstraOS\n");
        print("version  - Show OS version\n");
        print("history  - Show command history\n");
        print("uname    - System information\n");
        print("uptime   - Show system uptime\n");
        print("echo     - Print text\n");
        print("sleep    - Sleep for N seconds\n");
        print("ls       - List root directory (FAT16)\n");
        print("pwd      - Print working directory\n");
        print("cd       - Change directory\n");
        print("cat      - Display file contents\n");
        print("touch    - Create empty file\n");
        print("diskread - Read disk sector 0 (test)\n");
        print("disktest - Write + read test sector\n");
        print("fatinfo  - Show FAT16 boot sector info\n");
        print("halt     - Halt the CPU\n");
        print("reboot   - Reboot the system\n");
    }
    else if (strcmp(cmd, "clear") == 0)
    {
        clear_screen();
    }
    else if (strcmp(cmd, "about") == 0)
    {
        print("\nAstraOS - A custom kernel written from scratch.\n");
        print("Developer: Somen\n");
    }
    else if (strcmp(cmd, "version") == 0)
    {
        print("\nAstraOS version 0.1\n");
    }
    else if (strcmp(cmd, "history") == 0)
    {
        print("\nCommand History:\n");

        for (int i = 0; i < history_count; i++)
        {
            print("  ");
            print(history[i]);
            print("\n");
        }
    }
    else if (strcmp(cmd, "uname") == 0)
    {
        print("\nAstraOS Kernel 0.1 i386\n");
    }
    else if (strcmp(cmd, "uptime") == 0)
    {
        uint32_t t = timer_get_ticks();
        uint32_t seconds = t / 100;

        print("\nUptime: ");
        print_uint(seconds);
        print(" seconds\n");
    }
    else if (strcmp(cmd, "echo") == 0)
    {
        if (args)
        {
            print("\n");
            print(args);
            print("\n");
        }
        else
        {
            print("\n\n");
        }
    }
    else if (strcmp(cmd, "sleep") == 0)
    {
        if (!args)
        {
            print("\nUsage: sleep <seconds>\n");
        }
        else
        {
            uint32_t sec = 0;

            for (int i = 0; args[i] != '\0'; i++)
            {
                if (args[i] < '0' || args[i] > '9')
                {
                    print("\nInvalid number.\n");
                    return;
                }
                sec = sec * 10 + (args[i] - '0');
            }

            print("\nSleeping...\n");
            timer_sleep(sec);
            print("Done.\n");
        }
    }
    else if (strcmp(cmd, "diskread") == 0)
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
    }
    else if (strcmp(cmd, "fatinfo") == 0)
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
    }
    else if (strcmp(cmd, "halt") == 0)
    {
        print("\nSystem halting...\n");
        cpu_halt();
    }
    else if (strcmp(cmd, "reboot") == 0)
    {
        print("\nSystem rebooting...\n");
        cpu_reboot();
    }
    else if (strcmp(cmd, "ls") == 0)
    {

        if (!fat16_init())
        {
            print("\nFAT16 init failed.\n");
            return;
        }

        fat16_ls();
    }
    else if (strcmp(cmd, "cat") == 0)
    {

        if (!args)
        {
            print("\nUsage: cat <filename>\n");
            return;
        }

        if (!fat16_init())
        {
            print("\nFAT16 init failed.\n");
            return;
        }

        if (!fat16_cat(args))
        {
            print("\nFile not found.\n");
        }
    }
    else if (strcmp(cmd, "pwd") == 0)
    {
        fat16_pwd();
    }
    else if (strcmp(cmd, "cd") == 0)
    {

        if (!args)
        {
            print("\nUsage: cd <dirname>\n");
            return;
        }

        if (!fat16_init())
        {
            print("\nFAT16 init failed.\n");
            return;
        }

        if (!fat16_cd(args))
        {
            print("\nDirectory not found.\n");
        }
    }
    else if (strcmp(cmd, "disktest") == 0)
    {

        uint8_t *buf = (uint8_t *)kmalloc(512);

        for (int i = 0; i < 512; i++)
        {
            buf[i] = 0;
        }

        buf[0] = 'A';
        buf[1] = 'S';
        buf[2] = 'T';
        buf[3] = 'R';
        buf[4] = 'A';

        ata_write_sector(10, buf);

        // clear buffer and read again
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
    }
    else if (strcmp(cmd, "touch") == 0)
    {

        if (!args)
        {
            print("\nUsage: touch <filename>\n");
            return;
        }

        if (!fat16_init())
        {
            print("\nFAT16 init failed.\n");
            return;
        }

        if (fat16_touch(args))
        {
            print("\nFile created.\n");
        }
        else
        {
            print("\nTouch failed.\n");
        }
    }

    else if (cmd[0] == '\0')
    {
        // empty command
    }
    else
    {
        print("\nUnknown command: ");
        print(cmd);
        print("\nType 'help' for commands.\n");
    }
}

void shell_init()
{
    print("Welcome to AstraOS Shell\n");
    print("Type 'help' to see available commands.\n");
    shell_prompt();
}

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

static void history_add(const char *cmd)
{

    if (cmd[0] == '\0')
    {
        return;
    }

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
    {
        len = SHELL_BUFFER_SIZE - 1;
    }

    for (int i = 0; i < len; i++)
    {
        command_buffer[i] = cmd[i];
    }

    command_buffer[len] = '\0';

    buffer_length = len;
    cursor_pos = len;

    redraw_command_line();
}
