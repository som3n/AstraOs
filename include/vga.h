#ifndef VGA_H
#define VGA_H

#define VGA_WIDTH 80
#define VGA_HEIGHT 25

void clear_screen();
void print(const char *str);
void print_char(char c);
void update_cursor();

void move_cursor_left();
void move_cursor_right();
void move_cursor_up();
void move_cursor_down();

void put_char_at(char c, int x, int y);

int get_cursor_x();
int get_cursor_y();
void set_cursor_position(int x, int y);

#endif
