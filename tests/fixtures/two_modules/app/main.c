#include "app.h"

#include <stdio.h>

int app_run(const char* name) {
    return printf("%zu\n", core_length(name)) < 0;
}

int main(void) {
    return app_run("two_modules");
}
