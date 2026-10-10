#include "api.h"
#include "api.h"

namespace {

__forceinline Checksum mix(Checksum sum, unsigned char byte) { return (sum << 5) + sum + byte; }

__declspec(noinline) Checksum finish(Checksum sum) { return sum ^ (sum >> 33); }

} // namespace

API Checksum __stdcall checksum(const unsigned char* data, unsigned __int32 size) {
    Checksum sum = 5381;
    for (unsigned __int32 i = 0; i < size; ++i) {
        sum = mix(sum, data[i]);
    }
    return finish(sum);
}

API int __cdecl report(Checksum value) {
    __if_exists(checksum) { return static_cast<int>(value & 0xff) + static_cast<int>(finish(1)); }
    return 0;
}
