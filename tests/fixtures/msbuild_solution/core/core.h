#pragma once

#ifdef CORE_EXPORTS
#define CORE_API __declspec(dllexport)
#else
#define CORE_API
#endif

CORE_API unsigned __int64 __stdcall core_stamp(unsigned __int32 seed);
CORE_API void __stdcall core_log(const char *text);
