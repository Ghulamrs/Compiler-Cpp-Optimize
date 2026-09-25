// stdlib.h - allocation, conversion, and leaving.
//
// The same bargain as <stdio.h>: prototypes only, checked against glibc by the
// suite building every case twice. See that file for why this is not a copy of
// the real header.
//
// malloc needed nothing new from the compiler when it first appeared here - it
// arrives through an ordinary prototype, and the cast that turns its block into
// an array of something is the declarator grammar doing its ordinary work.
#ifndef _CC1_STDLIB_H
#define _CC1_STDLIB_H

// Everything here is the C library, so it is named the way C names it.
// Without this the compiler would give every declaration below a C++
// linkage name and ask the linker for a symbol libc has never had.
extern "C" {


#include <stddef.h>

void *malloc(size_t);
void *calloc(size_t, size_t);
void *realloc(void *, size_t);
void free(void *);

void exit(int);
void abort(void);

int atoi(const char *);
char *getenv(const char *);
long atol(const char *);

// The one that says where it stopped. atol cannot report a failure at all - it
// gives back zero for "0" and for "banana" alike - so anything reading a
// number it did not write itself wants this instead, and gets the rest of the
// string as well as the value.
//
// char ** rather than const char **, which looks like an oversight and is not:
// it is what C89 says, so that the end pointer can be used to walk a string
// the caller owns. Declaring it the tidier way would make every correct
// program that passes a char ** fail to compile here.
//
// strtol and strtoul are its neighbours and take a base as well: 0 reads the
// prefix - 0x for sixteen, a leading 0 for eight - and 2 to 36 says outright.
double atof(const char *);
double strtod(const char *, char **);
long strtol(const char *, char **, int);
unsigned long strtoul(const char *, char **, int);

int abs(int);
long labs(long);

int rand(void);
void srand(unsigned int);

// The two that could not be written here until pointers to functions existed,
// and a good part of the reason they were wanted. A comparison function is the
// argument, and there is no way to spell one without this type.
void qsort(void *, size_t, size_t, int (*)(const void *, const void *));
void *bsearch(const void *, const void *, size_t, size_t,
              int (*)(const void *, const void *));


}  // extern "C"
#endif
