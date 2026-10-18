#include "helpers.h"

int helper(int value) { return value + 1; }

template <typename T> int never_instantiated(T value) { return helper(1) + static_cast<int>(value); }
