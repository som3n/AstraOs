#include "vga.h"
#include "drivers/ports.h"

#define VGA_WIDTH 80
#define VGA_HEIGHT 25

static int cursor_x = 0;
static int cursor_y = 0;

static char *video_memory = (char *)0xB8000;

void clear_screen() {
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        video_memory[i * 2] = ' ';
        video_memory[i * 2 + 1] = 0x0F;
    }

    cursor_x = 0;
    cursor_y = 0;
}

void update_cursor() {
    unsigned short pos = (cursor_y * VGA_WIDTH) + cursor_x;

    outb(0x3D4, 0x0F);
    outb(0x3D5, (unsigned char)(pos & 0xFF));

    outb(0x3D4, 0x0E);
    outb(0x3D5, (unsigned char)((pos >> 8) & 0xFF));
}

static void scroll_screen() {
    // If we are still inside visible screen, do nothing
    if (cursor_y < VGA_HEIGHT) {
        return;
    }

    // Move every line one row up
    for (int y = 1; y < VGA_HEIGHT; y++) {
        for (int x = 0; x < VGA_WIDTH; x++) {
            int from = (y * VGA_WIDTH + x) * 2;
            int to   = ((y - 1) * VGA_WIDTH + x) * 2;

            video_memory[to] = video_memory[from];
            video_memory[to + 1] = video_memory[from + 1];
        }
    }

    // Clear the last line
    int last_line = (VGA_HEIGHT - 1) * VGA_WIDTH;
    for (int x = 0; x < VGA_WIDTH; x++) {
        video_memory[(last_line + x) * 2] = ' ';
        video_memory[(last_line + x) * 2 + 1] = 0x0F;
    }

    cursor_y = VGA_HEIGHT - 1;
}

void move_cursor_left() {
    if (cursor_x > 0) {
        cursor_x--;
    } else if (cursor_y > 0) {
        cursor_y--;
        cursor_x = VGA_WIDTH - 1;
    }
    update_cursor();
}

void move_cursor_right() {
    if (cursor_x < VGA_WIDTH - 1) {
        cursor_x++;
    } else {
        cursor_x = 0;
        cursor_y++;
        scroll_screen();
    }
    update_cursor();
}

void move_cursor_up() {
    if (cursor_y > 0) {
        cursor_y--;
    }
    update_cursor();
}

void move_cursor_down() {
    if (cursor_y < VGA_HEIGHT - 1) {
        cursor_y++;
    }
    update_cursor();
}

static void put_char(char c) {

    // Newline handling
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;

        scroll_screen();
        update_cursor();
        return;
    }

    // Backspace handling
    if (c == '\b') {
        if (cursor_x > 0) {
            cursor_x--;
        } else if (cursor_y > 0) {
            cursor_y--;
            cursor_x = VGA_WIDTH - 1;
        } else {
            return;
        }

        int index = (cursor_y * VGA_WIDTH + cursor_x) * 2;
        video_memory[index] = ' ';
        video_memory[index + 1] = 0x0F;

        update_cursor();
        return;
    }

    // Normal character printing
    int index = (cursor_y * VGA_WIDTH + cursor_x) * 2;
    video_memory[index] = c;
    video_memory[index + 1] = 0x0F;

    cursor_x++;

    // If line ends, go to next line
    if (cursor_x >= VGA_WIDTH) {
        cursor_x = 0;
        cursor_y++;
    }

    scroll_screen();
    update_cursor();
}

void print(const char *str) {
    for (int i = 0; str[i] != '\0'; i++) {
        put_char(str[i]);
    }
}

void print_char(char c) {
    char str[2];
    str[0] = c;
    str[1] = '\0';
    print(str);
}

void put_char_at(char c, int x, int y) {
    int index = (y * VGA_WIDTH + x) * 2;
    video_memory[index] = c;
    video_memory[index + 1] = 0x0F;
}
int get_cursor_x() {
    return cursor_x;
}

int get_cursor_y() {
    return cursor_y;
}

void set_cursor_position(int x, int y) {
    cursor_x = x;
    cursor_y = y;
    update_cursor();
}
