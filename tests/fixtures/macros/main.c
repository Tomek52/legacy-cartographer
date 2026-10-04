#include <string.h>

#include "log.h"

#define RUN_TWICE(fn) do { fn(); fn(); } while (0)
#define DEFINE_GETTER(name) static int name(void) { return LOG(0); }

int log_value(int value) { return value; }
int fail(void) { return -1; }
static int step(void) { return LOG(1); }
DEFINE_GETTER(zero)

int main(void) {
    RUN_TWICE(step);
    return CHECKED(LOG((int)strlen("x"))) + zero();
}
