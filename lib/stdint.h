/* <stdint.h> - the fixed-width integers, C99 7.18, as the four targets lay
   them out: int is 32 bits on every one, long is 64 on the two LP64 targets
   and 32 on Windows and the C6000, long long is 64 everywhere, and a
   pointer is a long except on Windows, where it is a long long. */
#ifndef _CXX1_STDINT_H
#define _CXX1_STDINT_H

typedef signed char        int8_t;
typedef unsigned char      uint8_t;
typedef short              int16_t;
typedef unsigned short     uint16_t;
typedef int                int32_t;
typedef unsigned int       uint32_t;
typedef long long          int64_t;
typedef unsigned long long uint64_t;

typedef int8_t   int_least8_t;
typedef uint8_t  uint_least8_t;
typedef int16_t  int_least16_t;
typedef uint16_t uint_least16_t;
typedef int32_t  int_least32_t;
typedef uint32_t uint_least32_t;
typedef int64_t  int_least64_t;
typedef uint64_t uint_least64_t;
typedef int32_t  int_fast8_t;
typedef uint32_t uint_fast8_t;
typedef int32_t  int_fast16_t;
typedef uint32_t uint_fast16_t;
typedef int32_t  int_fast32_t;
typedef uint32_t uint_fast32_t;
typedef int64_t  int_fast64_t;
typedef uint64_t uint_fast64_t;

#if defined(_WIN32)
typedef long long          intptr_t;
typedef unsigned long long uintptr_t;
#elif defined(__TMS320C6X__)
typedef int                intptr_t;
typedef unsigned int       uintptr_t;
#else
typedef long               intptr_t;
typedef unsigned long      uintptr_t;
#endif
typedef int64_t  intmax_t;
typedef uint64_t uintmax_t;

#define INT8_MIN   (-128)
#define INT8_MAX   127
#define UINT8_MAX  255
#define INT16_MIN  (-32768)
#define INT16_MAX  32767
#define UINT16_MAX 65535
#define INT32_MIN  (-2147483647-1)
#define INT32_MAX  2147483647
#define UINT32_MAX 4294967295U
#define INT64_MIN  (-9223372036854775807LL-1)
#define INT64_MAX  9223372036854775807LL
#define UINT64_MAX 18446744073709551615ULL
#define INTMAX_MIN INT64_MIN
#define INTMAX_MAX INT64_MAX
#define UINTMAX_MAX UINT64_MAX
#define SIZE_MAX   UINTPTR_MAX
#if defined(_WIN32)
#define INTPTR_MIN  INT64_MIN
#define INTPTR_MAX  INT64_MAX
#define UINTPTR_MAX UINT64_MAX
#elif defined(__TMS320C6X__)
#define INTPTR_MIN  INT32_MIN
#define INTPTR_MAX  INT32_MAX
#define UINTPTR_MAX UINT32_MAX
#else
#define INTPTR_MIN  INT64_MIN
#define INTPTR_MAX  INT64_MAX
#define UINTPTR_MAX UINT64_MAX
#endif
#define INT8_C(v)   v
#define UINT8_C(v)  v
#define INT16_C(v)  v
#define UINT16_C(v) v
#define INT32_C(v)  v
#define UINT32_C(v) v ## U
#define INT64_C(v)  v ## LL
#define UINT64_C(v) v ## ULL

#endif
