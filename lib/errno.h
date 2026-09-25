// errno.h - the last error number.
//
// errno is required to be a modifiable lvalue and is *not* required to be a
// variable, which is the whole difficulty. Every one of these platforms makes
// it thread-local by hiding a function behind a macro, and each hides a
// different one:
//
//     glibc     (*__errno_location())
//     macOS     (*__error())
//     UCRT      (*_errno())
//
// A header declaring 'extern int errno;' would link on none of them. So the
// function is declared and the macro dereferences it, which is what the
// platform's own header does.
//
// Only EDOM and ERANGE are C90's; everything else a program tests for is
// POSIX. The two that C90 requires agree across all three targets at 33 and 34
// - measured rather than assumed - and the rest are given their real values
// per platform, because a program comparing errno to ENOENT wants the number
// its own C library will set.
#ifndef _CC1_ERRNO_H
#define _CC1_ERRNO_H

// Everything here is the C library, so it is named the way C names it.
// Without this the compiler would give every declaration below a C++
// linkage name and ask the linker for a symbol libc has never had.
extern "C" {


#ifdef _WIN32
int *_errno(void);
#define errno (*_errno())
#elif defined(__APPLE__)
int *__error(void);
#define errno (*__error())
#elif defined(__TMS320C6X__)
/* TI's spelling under the EABI, and the VM6747 emulator answers to it. */
int *__c6xabi_errno_addr(void);
#define errno (*__c6xabi_errno_addr())
#else
int *__errno_location(void);
#define errno (*__errno_location())
#endif

// C90's two, and the same number everywhere.
#define EDOM    33
#define ERANGE  34

// EILSEQ is C95 and genuinely different on each platform, which is why it is
// worth guarding rather than picking one: 84 under glibc, 92 on macOS, 42
// under the UCRT.
#ifdef _WIN32
#define EILSEQ  42
#elif defined(__APPLE__)
#define EILSEQ  92
#else
#define EILSEQ  84
#endif

// The handful a C90 program actually tests after a failed fopen or malloc.
// POSIX rather than ISO, and the low numbers happen to agree on all three.
#define EPERM   1
#define ENOENT  2
#define EINTR   4
#define EIO     5
#define EBADF   9
#define ENOMEM  12
#define EACCES  13
#define EEXIST  17
#define EINVAL  22
#define ENOSPC  28


}  // extern "C"
#endif
