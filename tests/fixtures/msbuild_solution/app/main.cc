#include <cstdio>

#include "core.h"

#ifndef APP_GREETING
#error APP_GREETING must come from the project's PreprocessorDefinitions
#endif

int main() {
  std::printf("%s\n", APP_GREETING);
  return core_stamp(1) == 0 ? 1 : 0;
}
