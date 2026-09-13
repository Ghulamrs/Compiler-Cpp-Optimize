// stddef.h - the types every other header here needs.
//
// It exists so that size_t is declared once. A typedef repeated in two headers
// is a redefinition, and a program including both would be refused for a
// mistake it did not make.
//
// A header is text and cannot consult the compiler's type table, so each of
// these is spelled out per target, and the suite is what checks they agree: a
// case printing the sizes is built by the platform's own compiler as well,
// reading the platform's own stddef.h rather than this one.
#ifndef _CC1_STDDEF_H
#define _CC1_STDDEF_H

// Everything here is the C library, so it is named the way C names it.
// Without this the compiler would give every declaration below a C++
// linkage name and ask the linker for a symbol libc has never had.
extern "C" {


#ifdef _WIN32

// LLP64, and **unsigned long is four bytes here** - so size_t has to be the
// long long. This file said 'unsigned long' unconditionally until wchar_t was
// added and the sizes were measured on all three targets rather than assumed:
// sizeof(size_t) came out 4 on this one against 8 for a pointer, so a size and
// an address were different widths and anything keeping one in the other lost
// half of it.
typedef unsigned long long size_t;
typedef long long          ptrdiff_t;

#elif defined(__TMS320C6X__)

// ILP32 on the C6000, and TI's headers say `unsigned int`, not `unsigned
// long` - the same width, but a different type, and in C++ a different
// letter in every mangled name that takes a size.
typedef unsigned int size_t;
typedef int          ptrdiff_t;

#else

// LP64 on Linux and macOS alike: long is eight bytes and matches a pointer.
typedef unsigned long size_t;
typedef long          ptrdiff_t;

#endif

// glibc spells this ((void *)0). Here it is 0, because a null pointer constant
// in C is an integer constant expression with value zero, and this compiler
// accepts that in every place a pointer is wanted. The difference shows only
// where NULL is passed to a variadic function, which is a mistake in either
// spelling.
#define NULL 0

// The offset of a member, which C has no other way to ask for: take the
// address of the member of a struct that begins at address zero, and the
// address *is* the offset. Nothing is dereferenced - only an address is worked
// out - which is why this is not the wild pointer it looks like.
#define offsetof(type, member) ((size_t)&(((type *)0)->member))


}  // extern "C"
#endif
