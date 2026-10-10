#include "api.h"

// cl defines _DLL for /MD (also spelled -MD), not for /MT.
#ifndef _DLL
#error _DLL is not defined: the -MD option did not reach the driver
#endif

API int __cdecl report_runtime() { return report(checksum(nullptr, 0)); }
