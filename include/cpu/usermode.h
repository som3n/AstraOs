#ifndef USERMODE_H
#define USERMODE_H

int switch_to_user_mode(uint32_t entry_eip, uint32_t user_stack_top);
__attribute__((noreturn)) void usermode_exit(int code);

#endif
