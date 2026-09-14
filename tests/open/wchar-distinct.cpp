// wchar_t is a distinct type in C++ ([basic.fundamental]/5), with its own
// mangling `w` and its own place in overload resolution, whatever integer
// type it shares a representation with. cxx1 takes it as that integer type
// - int on the Itanium hosts, unsigned short on Windows and the C6000 - so
// f(wchar_t) and f(int) are one function here, and a template instantiated
// on wchar_t is named as if on int. Found while making the C6000's wchar_t
// two bytes, where the width came out right and the letter did not.
extern "C" int printf(const char *, ...);
static int f(int) { return 1; }
static int f(wchar_t) { return 2; }
template <class T> struct Is { static const int w = 0; };
template <> struct Is<wchar_t> { static const int w = 1; };
int main() { printf("%d %d %d\n", f(L'x'), f(7), Is<wchar_t>::w); return 0; }
