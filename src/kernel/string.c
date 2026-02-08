#include "string.h"

int strlen(const char *str) {
    int i = 0;
    while (str[i]) i++;
    return i;
}

int strcmp(const char *a, const char *b) {
    int i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return a[i] - b[i];
        i++;
    }
    return a[i] - b[i];
}

void strcpy(char *dest, const char *src) {
    int i = 0;
    while (src[i]) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

int strncmp(const char *s1, const char *s2, int n)
{
    for (int i = 0; i < n; i++)
    {
        unsigned char c1 = s1[i];
        unsigned char c2 = s2[i];

        if (c1 != c2)
            return c1 - c2;

        if (c1 == '\0')
            return 0;
    }

    return 0;
}
