#include "shared.h"

static int helper(void);

int right_value(void) {
    return helper() + library_only(0);
}

static int helper(void)
{
    return 2;
}
