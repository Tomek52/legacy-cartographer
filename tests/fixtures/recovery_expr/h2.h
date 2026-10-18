#ifndef RECOVERY_EXPR_H2
#define RECOVERY_EXPR_H2

#include "h1.h"

static int k(void) {
    struct S s = {0};
    return g(s, 1);
}

#endif
