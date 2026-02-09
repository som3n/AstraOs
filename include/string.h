#ifndef STRING_H
#define STRING_H

int strlen(const char *str);
int strcmp(const char *a, const char *b);
void strcpy(char *dest, const char *src);
char* strcat(char *dest, const char *src);

void *memset(void *dest, int val, unsigned int len);

#endif
