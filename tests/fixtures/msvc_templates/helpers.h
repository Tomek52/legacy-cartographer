#pragma once

int helper(int value);

template <typename T> int relay(T value) { return helper(2) + static_cast<int>(value); }
