// math.h - the C90 mathematical functions.
//
// The same bargain as <stdio.h>: prototypes only, and the real implementations
// arrive from the host's libm at link time. The suite builds every case twice,
// once with cc1 and once with the reference compiler, so a prototype that
// disagreed with the platform's would be caught by the answer coming out wrong
// rather than by anything here.
//
// Everything in C90's <math.h> is 'double' in and 'double' out. There are no
// float or long double variants - 'sqrtf' and 'sqrtl' are C99, and 'long
// double' is not a type this compiler has - so a program wanting a float result
// converts one, which is what C90 programs did.
//
// The three that are not simply 'double f(double)' are the ones that return a
// second answer through a pointer: frexp splits a number into a fraction and an
// exponent, modf into a fractional and an integral part, and ldexp puts a
// frexp'd pair back together.
#ifndef _CC1_MATH_H
#define _CC1_MATH_H

// Everything here is the C library, so it is named the way C names it.
// Without this the compiler would give every declaration below a C++
// linkage name and ask the linker for a symbol libc has never had.
extern "C" {


// C90 requires HUGE_VAL to be a positive double, and permits it to be infinite.
// 1e400 has no finite double to round to, so it folds to +inf here exactly as
// it does under gcc - which was checked rather than assumed before writing it.
#define HUGE_VAL 1e400

// The M_ constants are **not in C90**, and are here on purpose.
//
// They are POSIX rather than ISO, and every implementation ships them - glibc
// unless __STRICT_ANSI__, macOS outright, MSVC behind _USE_MATH_DEFINES. A
// program that computes an angle writes M_PI and does not think of itself as
// reaching for an extension, so leaving them out means the first real
// trigonometric program written against this compiler does not build. That is
// a worse answer than being one macro wider than the standard.
//
// Written to more digits than a double can hold, so the value rounds to the
// nearest representable one rather than to whatever a shorter literal reaches.
#define M_E         2.71828182845904523536
#define M_LOG2E     1.44269504088896340736
#define M_LOG10E    0.43429448190325182765
#define M_LN2       0.69314718055994530942
#define M_LN10      2.30258509299404568402
#define M_PI        3.14159265358979323846
#define M_PI_2      1.57079632679489661923
#define M_PI_4      0.78539816339744830962
#define M_1_PI      0.31830988618379067154
#define M_2_PI      0.63661977236758134308
#define M_2_SQRTPI  1.12837916709551257390
#define M_SQRT2     1.41421356237309504880
#define M_SQRT1_2   0.70710678118654752440

double acos(double);
double asin(double);
double atan(double);
double atan2(double, double);
double cos(double);
double sin(double);
double tan(double);

double cosh(double);
double sinh(double);
double tanh(double);

double exp(double);
double log(double);
double log10(double);

// The exponent travels back through the pointer; the return is the fraction.
double frexp(double, int *);
double ldexp(double, int);
// The integral part travels back through the pointer; the return is the rest.
double modf(double, double *);

double pow(double, double);
double sqrt(double);

double ceil(double);
double fabs(double);
double floor(double);
double fmod(double, double);
/* C99 */
double trunc(double);
double round(double);


}  // extern "C"
#endif
