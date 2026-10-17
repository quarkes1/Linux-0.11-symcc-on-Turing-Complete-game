#ifndef SDK_STDINT_H
#define SDK_STDINT_H
typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef short int16_t;
typedef unsigned short uint16_t;
typedef int int32_t;
typedef unsigned uint32_t;
typedef unsigned uintptr_t;
typedef int intptr_t;
#define UINT32_MAX 0xffffffffu
#define INT32_MAX 2147483647
#define INT32_MIN (-2147483647-1)
#endif
