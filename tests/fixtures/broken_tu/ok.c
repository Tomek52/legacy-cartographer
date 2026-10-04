#include "util.h"

#include <stdio.h>

size_t util_length(const char* text) {
    size_t length = 0;
    while (text[length] != '\0') {
        ++length;
    }
    return length;
}

int main(void) {
    printf("%zu\n", util_length("abc"));
    return 0;
}
