// stdio.h - the part of it this compiler can hold.
//
// Not glibc's header, and not a copy of one. Reaching glibc's <stdio.h> means
// reading 24 files and 744 lines carrying 107 uses of __restrict, 57 of
// __attribute__, 15 of long double and a scattering of __extension__, __asm__,
// __inline and __typeof - none of it C, and all of it in the way before a
// single usable declaration. What a program wants from stdio.h is the
// prototypes, and a prototype is ordinary C.
//
// These declarations must agree with glibc's, because the program links against
// glibc. Nothing here is taken on trust: every test case is built a second time
// by gcc, which reads the real header, and the two binaries must produce the
// same bytes and the same exit status. A prototype that lied would fail there -
// which is why the suite deliberately does not pass -I to gcc.
#ifndef _CC1_STDIO_H
#define _CC1_STDIO_H

// Everything here is the C library, so it is named the way C names it.
// Without this the compiler would give every declaration below a C++
// linkage name and ask the linker for a symbol libc has never had.
extern "C" {


#include <stddef.h>

// FILE is an incomplete struct reached only through a pointer, which is exactly
// how glibc declares it and exactly what this compiler supports. It was left
// out of this header for a while on the belief that an opaque handle needed
// something the compiler did not have; that was wrong, and the file functions
// below have worked from the day incomplete types did.
//
// The name inside the typedef is glibc's own. It need not be - nothing here can
// ever see the definition - but matching it means a reader comparing this file
// against the real one is not left wondering whether they are the same type.
typedef struct _IO_FILE FILE;

// The three standard streams, which are the one place in this header where the
// platforms genuinely disagree - not about what C says, but about what the
// symbol is called once the linker goes looking for it. C requires only that
// stdin, stdout and stderr be expressions of type FILE *. It does not say they
// are objects, and that is exactly the room these three take.
//
// Getting this wrong does not misbehave, it fails to link, and the message
// names a symbol the program never wrote: "undefined symbol ___stdoutp".
#if defined(__APPLE__)
// Darwin exports them under underscored names and defines the plain ones as
// macros, which is what its own <stdio.h> does. Read off 'nm' against what
// clang emits for the same program rather than taken on trust.
extern FILE *__stdinp;
extern FILE *__stdoutp;
extern FILE *__stderrp;
#define stdin  __stdinp
#define stdout __stdoutp
#define stderr __stderrp
#elif defined(_WIN32)
// Microsoft's UCRT keeps the three in an array reached through a function, so
// 'stdout' is a call and not an object at all. The same move as the one behind
// legacy_stdio_definitions: things left the library for the header, and a
// compiler shipping its own header has to follow them.
FILE *__acrt_iob_func(unsigned);
#define stdin  __acrt_iob_func(0)
#define stdout __acrt_iob_func(1)
#define stderr __acrt_iob_func(2)
#elif defined(__TMS320C6X__)
// TI keeps the three at the front of a table of its own FILE - six words,
// laid out here because &_ftable[1] needs the size - and the VM6747 emulator
// carries a table of the same stride, so one spelling links against both.
struct _IO_FILE {
    int fd;
    unsigned char *buf, *pos, *bufend, *buff_stop;
    unsigned int flags;
};
extern FILE _ftable[20];
#define stdin  (&_ftable[0])
#define stdout (&_ftable[1])
#define stderr (&_ftable[2])
#else
// glibc, where they are ordinary exported objects and the plain names work.
extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;
#endif

#define EOF (-1)

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

// Opening and closing.
FILE *fopen(const char *, const char *);
FILE *freopen(const char *, const char *, FILE *);
FILE *tmpfile(void);
int fclose(FILE *);
int fflush(FILE *);

// Formatted output and input.
// On Windows these are not all symbols. The UCRT keeps printf, fprintf, puts
// and fputs as real exports, and makes sprintf, the v-family and the scanf
// family inline wrappers over __stdio_common_* in its own <stdio.h>. Declaring
// them as the ordinary functions C says they are is right, and it is why a
// Windows link needs legacy_stdio_definitions.lib - see tests/windows-native.
int printf(const char *, ...);
int fprintf(FILE *, const char *, ...);
int sprintf(char *, const char *, ...);
// **snprintf, which C99 added and C++11 requires.** `sprintf` cannot be used
// safely against a precision the program chooses: `os << setprecision(400)`
// on a `long double` asks for more digits than any fixed buffer holds, and
// LDBL_MAX in `%Lf` is nearly five thousand of them before the point. The
// bound is the whole reason this is declared - see <ostream>.
int snprintf(char *, size_t, const char *, ...);
int scanf(const char *, ...);
int fscanf(FILE *, const char *, ...);
int sscanf(const char *, const char *, ...);

// Characters and lines.
int fgetc(FILE *);
int fputc(int, FILE *);
int getc(FILE *);
int putc(int, FILE *);
int getchar(void);
int putchar(int);
int ungetc(int, FILE *);
char *fgets(char *, int, FILE *);
int fputs(const char *, FILE *);
int puts(const char *);

// Whole blocks, which is what "binary mode" amounts to: bytes in and out with
// no interpretation. fwrite takes const void * and fread void *, and a struct
// reaches them through '&' like anything else.
size_t fread(void *, size_t, size_t, FILE *);
size_t fwrite(const void *, size_t, size_t, FILE *);

// Position.
int fseek(FILE *, long, int);
long ftell(FILE *);
void rewind(FILE *);

// State.
int feof(FILE *);
int ferror(FILE *);
void clearerr(FILE *);
void perror(const char *);

// The files themselves.
int remove(const char *);
int rename(const char *, const char *);

// vprintf, vfprintf and vsprintf take a va_list, which needs <stdarg.h> - so
// they are declared there rather than here, and only a translation unit that
// asked for stdarg.h can see them. Declaring them unconditionally would put
// the name va_list into every file that includes stdio.h.
//
// Two absences remain, each for a reason rather than an oversight.
//
// fgetpos and fsetpos take an fpos_t *, and the caller has to declare the
// fpos_t. glibc's is a struct, so declaring one needs its definition, and its
// definition is the kind of thing this header exists to avoid. fseek and ftell
// do the same work with a long.
//
// setbuf and setvbuf are omitted only because nothing here has needed them; add
// them the day something does.

// The descriptor behind a stream. Spelled with a leading underscore because
// that is what the Microsoft CRT calls it and this is what <io.h>'s _setmode
// is given; POSIX's `fileno` is the same function under the name a program
// targeting glibc would write, and both are declared so that either spelling
// links on the platform that has it.
int _fileno(FILE *stream);
int fileno(FILE *stream);

}  // extern "C"
#endif
