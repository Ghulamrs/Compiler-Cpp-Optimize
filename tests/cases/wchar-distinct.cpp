// wchar_t is a distinct type in C++ ([basic.fundamental]/5), with its own
// mangling `w` and its own place in overload resolution, whatever integer
// type it shares a representation with - int on the Itanium hosts, unsigned
// short on Windows and the C6000, which its size and sign follow. cxx1 took
// it as that integer type, so f(wchar_t) and f(int) were one function and a
// template instantiated on wchar_t was named as if on int; the third review
// saw it from the link, `_Z7f_wchart` against cl6x's `_Z7f_wcharw`.
extern "C" int printf(const char *, ...);
static int f(int) { return 1; }
static int f(wchar_t) { return 2; }
template <class T> struct Is { static const int w = 0; };
template <> struct Is<wchar_t> { static const int w = 1; };
int main() { printf("%d %d %d\n", f(L'x'), f(7), Is<wchar_t>::w); return 0; }
