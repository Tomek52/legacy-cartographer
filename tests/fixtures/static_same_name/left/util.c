#include "shared.h"

static int helper(void) {
    return 1;
}

int left_value(void) {
    return twice(helper()) + callback();
}

static int callback(void) {
    return 10;
}
