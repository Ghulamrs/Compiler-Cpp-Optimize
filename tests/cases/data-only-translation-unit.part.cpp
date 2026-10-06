// The second translation unit of data-only-translation-unit.cpp: data, and no function at all.
// [basic.link] puts no function in a translation unit's requirements, and a run-time library is
// full of such files - a table of FILE, the type_info objects. cpp11 refused one until 2026-10-06.
//
// And the linkage of `extern "C"` written straight before a declaration, [dcl.link]/7: as if
// `extern` - so a const object here is external, where in braces it keeps internal linkage.
extern "C" const char version[] = "RTS6x";
extern "C" const int primes[5] = { 2, 3, 5, 7, 11 };
extern "C" int counter = 40;
extern "C" { const int hidden = 99; }
extern "C" typedef int number;
double scale = 2.5;
const char *names[2] = { "a", "b" };
number unused = 0;
// Its address taken, so that clang emits it as cxx1 does and the names suite compares names.
const int *partHidden = &hidden;
