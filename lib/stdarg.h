#ifndef _STDARG_H
#define _STDARG_H

// Everything here is the C library, so it is named the way C names it.
// Without this the compiler would give every declaration below a C++
// linkage name and ask the linker for a symbol libc has never had.
extern "C" {


/* Three targets, two va_lists, and the split is not where you would guess.
 *
 * va_list is the ABI's choice and not this compiler's: vprintf lives in the C
 * library and reads whatever the platform says a va_list is, so what is below
 * has to match it byte for byte or the first forwarded call walks the wrong
 * memory.
 *
 * System V is the odd one. It passes variadic arguments in registers like any
 * other, so the callee spills them to a save area and the va_list has to
 * describe two of those - a four-field record holding an offset into each
 * register file and two pointers. It is declared as an array of one so that
 * passing it decays to a pointer and the callee's walk is visible to the
 * caller, which is what the standard requires of va_list without saying how.
 *
 * Microsoft x64 and Apple's arm64 both make it a plain char *, and for
 * different reasons that arrive at the same shape. Windows gives every
 * argument, named or not, a consecutive eight-byte slot starting at the shadow
 * space the caller already left, and sends a variadic float in the integer
 * register as well as the vector one - so eight bytes read the right thing
 * whatever the type. Apple simply departs from AAPCS64 and puts the whole
 * variadic part on the stack in eight-byte slots, never in registers. Two
 * different rules, one pointer walk.
 *
 * __builtin_va_start takes a pointer to the va_list object in every case.
 * System V gets one for free, an array of one decaying; the other two have to
 * take the address, which is the only reason the macros differ in shape.
 */
/* The C6000 is a fourth target and the third char *: TI's convention puts the
 * last named argument and everything after it on the stack, a word each and
 * an 8-byte value at an 8-byte boundary, so one pointer walks them. */
#if defined(_WIN32) || (defined(__APPLE__) && defined(__aarch64__)) || defined(__TMS320C6X__)

typedef char *va_list;
#define va_start(ap, last) __builtin_va_start(&(ap))
#define va_arg(ap, type)   __builtin_va_arg(&(ap), type)

#else

typedef struct {
    unsigned int gp_offset;
    unsigned int fp_offset;
    void *overflow_arg_area;
    void *reg_save_area;
} __va_list_tag;

typedef __va_list_tag va_list[1];
#define va_start(ap, last) __builtin_va_start(ap)
#define va_arg(ap, type)   __builtin_va_arg(ap, type)

#endif

/* The second argument to va_start above is accepted and ignored. Which
 * parameters were named is a property of the definition and the compiler
 * already knows it; asking the caller to name the last one again is a
 * convention from when this was written in C rather than known to the front
 * end. */
#define va_end(ap)         ((void)0)

/* The three <stdio.h> functions that take a va_list. They live here and not
 * there because that is where va_list is: a file that never asked for stdarg.h
 * has no use for them and should not have the name in scope. */
int vprintf(const char *, va_list);
int vfprintf(void *, const char *, va_list);
int vsprintf(char *, const char *, va_list);


}  // extern "C"
#endif
