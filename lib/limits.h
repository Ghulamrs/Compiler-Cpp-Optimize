// limits.h - the ranges of the integer types, as this target has them.
//
// Every value here is a property of the target rather than of the standard,
// and the compiler already knows all of them: it predefines __SIZEOF_INT__ and
// the rest before reading a line. What it cannot do is hand them over as
// limits, because C has no way to write "the largest value of a type" as an
// expression - so they are spelled out, and the one that actually varies is
// guarded.
//
// LONG is the only width that moves between the three targets. Linux and macOS
// are LP64, where long is eight bytes; Windows is LLP64, where it is four and
// only long long is eight. That single difference is why this file has an
// #ifdef in it and <float.h> does not.
//
// The negative minima are written as (-MAX - 1) rather than as the literal.
// -2147483648 is not an int constant in C: it is the unary minus of
// 2147483648, which does not fit in an int, so writing it directly gives the
// value the wrong type. This spelling is the standard's own.
#ifndef _CC1_LIMITS_H
#define _CC1_LIMITS_H

// Everything here is the C library, so it is named the way C names it.
// Without this the compiler would give every declaration below a C++
// linkage name and ask the linker for a symbol libc has never had.
extern "C" {


#define CHAR_BIT    8

// Plain char is signed on all three of these targets, which the compiler also
// answers for - Target::plainCharIsSigned. A target where it was unsigned
// would need CHAR_MIN 0 and CHAR_MAX 255.
#define SCHAR_MIN   (-128)
#define SCHAR_MAX   127
#define UCHAR_MAX   255
#define CHAR_MIN    SCHAR_MIN
#define CHAR_MAX    SCHAR_MAX

#define SHRT_MIN    (-32768)
#define SHRT_MAX    32767
#define USHRT_MAX   65535

#define INT_MIN     (-INT_MAX - 1)
#define INT_MAX     2147483647
#define UINT_MAX    4294967295U

#if defined(_WIN32) || defined(__TMS320C6X__)

// A four-byte long: Windows' LLP64, and the C6000's ILP32 (SPRAB89B 2.1) - which had the 64-bit
// values here until RTS6x's strtol tested them (2026-10-06).
#define LONG_MIN    (-LONG_MAX - 1)
#define LONG_MAX    2147483647L
#define ULONG_MAX   4294967295UL

#else

#define LONG_MIN    (-LONG_MAX - 1)
#define LONG_MAX    9223372036854775807L
#define ULONG_MAX   18446744073709551615UL

#endif

// long long is C99 rather than C90, and this compiler accepts it as one of its
// eight deliberate extensions - see docs/STATUS.md. The limits come with it,
// because a program using the type and denied the limits is in a worse
// position than one that has neither.
#define LLONG_MIN   (-LLONG_MAX - 1)
#define LLONG_MAX   9223372036854775807LL
#define ULLONG_MAX  18446744073709551615ULL

// The largest multibyte character in any supported locale. Genuinely different
// on all three - measured, not assumed: 16 under glibc, 6 on macOS, 5 under the
// UCRT. A program sizing a buffer by it gets its own platform's answer.
#ifdef _WIN32
#define MB_LEN_MAX  5
#elif defined(__APPLE__)
#define MB_LEN_MAX  6
#else
#define MB_LEN_MAX  16
#endif


}  // extern "C"
#endif
