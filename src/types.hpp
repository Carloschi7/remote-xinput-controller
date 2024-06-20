#include <stdint.h>

#if defined WIN32 || defined _MSC_VER || defined __linux__
//Linked with x64 bit libs, so this is only x64 by default
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

static_assert(sizeof(float) == 4, "f32 is not 32-bit on this system");
static_assert(sizeof(double) == 8, "f64 is not 64-bit on this system");

typedef float f32;
typedef double f64;
#else
static_assert(0, "This OS is not supported yet");
#endif