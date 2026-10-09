#pragma once

#ifdef API_EXPORTS
#define API __declspec(dllexport)
#else
#define API __declspec(dllimport)
#endif

typedef unsigned __int64 Checksum;

API Checksum __stdcall checksum(const unsigned char* data, unsigned __int32 size);
API int __cdecl report(Checksum value);
