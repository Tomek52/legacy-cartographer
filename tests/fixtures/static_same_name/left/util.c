#include "shared.h"

static int helper(void) {
    return 1;
}

int left_value(void) {
    return helper();
}
