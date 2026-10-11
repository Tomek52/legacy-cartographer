#include <windows.h>

#include "core.h"

namespace {
__forceinline unsigned __int64 core_mix(unsigned __int64 value) {
  return (value << 7) ^ (value >> 3);
}
} // namespace

void __stdcall core_log(const char *text) { OutputDebugStringA(text); }

unsigned __int64 __stdcall core_stamp(unsigned __int32 seed) {
  core_log("core_stamp");
  return core_mix(GetTickCount64() + seed);
}
