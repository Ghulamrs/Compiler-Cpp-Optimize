// long double is 8 bytes on Windows and a distinct type; wchar_t is distinct from unsigned short for overloading.
#include <cstdio>
const char *w(wchar_t) { return "wchar_t"; }
const char *w(unsigned short) { return "ushort"; }
const char *w(int) { return "int"; }
const char *ld(long double) { return "long double"; }
const char *ld(double) { return "double"; }
int main() { unsigned short us = 1; std::printf("%s %s %s %s %s %d\n", w(L'a'), w(us), w(1), ld(1.0L), ld(1.0), (int)sizeof(long double)); return 0; }
