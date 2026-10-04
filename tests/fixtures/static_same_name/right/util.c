#include "shared.h"

static int helper(void);

int right_value(void) {
    return twice(helper()) + library_only(0) + callback();
}

static int helper(void)
{
    return 2;
}

static int callback(void) {
    return 20;
}
