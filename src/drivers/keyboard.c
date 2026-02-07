#include "drivers/keyboard.h"
#include "drivers/ports.h"
#include "cpu/irq.h"
#include "shell.h"
#include "keys.h"

#define KEYBOARD_DATA_PORT 0x60

static int shift_pressed = 0;
static int caps_lock = 0;
static int extended_scancode = 0;

// Normal US QWERTY
static char keymap[128] = {
    0,  27, '1','2','3','4','5','6','7','8','9','0','-','=', '\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n', 0,
    'a','s','d','f','g','h','j','k','l',';','\'','`', 0,'\\',
    'z','x','c','v','b','n','m',',','.','/', 0,
    '*', 0,' ', 0,
};

// Shifted US QWERTY
static char keymap_shift[128] = {
    0,  27, '!','@','#','$','%','^','&','*','(',')','_','+', '\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n', 0,
    'A','S','D','F','G','H','J','K','L',':','"','~', 0,'|',
    'Z','X','C','V','B','N','M','<','>','?', 0,
    '*', 0,' ', 0,
};

static char apply_caps(char c) {
    if (c >= 'a' && c <= 'z') return c - 32;
    if (c >= 'A' && c <= 'Z') return c + 32;
    return c;
}

static void keyboard_callback(registers_t *r) {
    (void)r;

    uint8_t scancode = inb(KEYBOARD_DATA_PORT);

    // Extended scancode prefix (E0)
    if (scancode == 0xE0) {
        extended_scancode = 1;
        return;
    }

    // Key release event
    if (scancode & 0x80) {
        uint8_t key = scancode & 0x7F;

        // If this was an extended key release, clear flag safely
        if (extended_scancode) {
            extended_scancode = 0;
            return;
        }

        // Shift release
        if (key == 0x2A || key == 0x36) {
            shift_pressed = 0;
        }

        return;
    }

    // Extended key handling (E0 prefix already received)
    if (extended_scancode) {
        extended_scancode = 0;

        switch (scancode) {
            case 0x48: // Up
                shell_handle_input(KEY_ARROW_UP);
                return;

            case 0x50: // Down
                shell_handle_input(KEY_ARROW_DOWN);
                return;

            case 0x4B: // Left
                shell_handle_input(KEY_ARROW_LEFT);
                return;

            case 0x4D: // Right
                shell_handle_input(KEY_ARROW_RIGHT);
                return;

            case 0x53: // Delete
                shell_handle_input(KEY_DELETE);
                return;

            default:
                return;
        }
    }

    // Shift press
    if (scancode == 0x2A || scancode == 0x36) {
        shift_pressed = 1;
        return;
    }

    // CapsLock press
    if (scancode == 0x3A) {
        caps_lock = !caps_lock;
        return;
    }

    // Normal character keys
    char c = 0;

    if (shift_pressed) {
        c = keymap_shift[scancode];
    } else {
        c = keymap[scancode];
    }

    // CapsLock affects only letters
    if (caps_lock && (c >= 'a' && c <= 'z') && !shift_pressed) {
        c = apply_caps(c);
    } else if (caps_lock && (c >= 'A' && c <= 'Z') && shift_pressed) {
        c = apply_caps(c);
    }

    if (c) {
        shell_handle_input((int)c);
    }
}

void keyboard_init() {
    irq_register_handler(1, keyboard_callback);
}
