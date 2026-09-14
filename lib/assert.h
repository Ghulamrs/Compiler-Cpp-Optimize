// assert.h - the diagnostic that a condition held.
//
// The one header in the standard that is *required* to be re-readable: NDEBUG
// is consulted at every #include, not once, so a file may include this twice
// with the macro defined in between and get both behaviours. That is why the
// include guard here covers only the declarations and not the macro, which is
// undefined and redefined on every pass. Guarding the whole file, as every
// other header here does, would be the ordinary thing to do and would be
// wrong.
//
// Each platform reports a failed assertion through its own function, with the
// arguments in its own order:
//
//     glibc   __assert_fail(expr, file, line, function)
//     macOS   __assert_rtn(function, file, line, expr)
//     UCRT    _assert(expr, file, line)
//
// None returns. Calling abort() instead would be simpler and would lose the
// message, which is the only part of an assertion anybody reads.
//
// __func__ is C99 and this compiler does not have it, so the function name is
// given as a literal rather than left out - the platforms that want the
// argument want something there.
#ifndef _CC1_ASSERT_H
#define _CC1_ASSERT_H

// Everything here is the C library, so it is named the way C names it.
// Without this the compiler would give every declaration below a C++
// linkage name and ask the linker for a symbol libc has never had.
extern "C" {


#ifdef _WIN32
void _assert(const char *expr, const char *file, unsigned line);
#elif defined(__APPLE__)
void __assert_rtn(const char *func, const char *file, int line, const char *expr);
#elif defined(__TMS320C6X__)
/* TI's assert builds the whole message in the macro and hands it to this,
   which its runtime defines and the VM6747 emulator answers to. */
void __c6xabi_abort_msg(const char *msg);
#define _CC1_STR(x) _CC1_STR2(x)
#define _CC1_STR2(x) #x
#else
void __assert_fail(const char *expr, const char *file, unsigned line,
                   const char *func);
#endif

// **The `extern "C"` closes inside the guard**, with the declarations it
// brackets. It opened inside the guard, so its `}` must too, or a second
// include - which is allowed, the macro being re-read every time - skips the
// open and emits an orphan `}` at file scope. cassert included both directly
// and through another header is exactly that second include.
}  // extern "C"

#endif

// Outside the guard on purpose - see above.
#undef assert

#ifdef NDEBUG

// C90: with NDEBUG defined, assert expands to something with no effect. The
// cast to void is what keeps 'assert(x);' a statement and stops a compiler
// complaining that the result is unused.
#define assert(e) ((void)0)

#else

#ifdef _WIN32
#define assert(e) ((e) ? (void)0 : _assert(#e, __FILE__, __LINE__))
#elif defined(__APPLE__)
#define assert(e) ((e) ? (void)0 : __assert_rtn("(unknown)", __FILE__, __LINE__, #e))
#elif defined(__TMS320C6X__)
#define assert(e) ((e) ? (void)0 : __c6xabi_abort_msg("Assertion failed, (" #e "), file " __FILE__ ", line " _CC1_STR(__LINE__) "\n"))
#else
#define assert(e) ((e) ? (void)0 : __assert_fail(#e, __FILE__, __LINE__, "(unknown)"))
#endif

#endif
