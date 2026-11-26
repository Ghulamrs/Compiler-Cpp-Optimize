// A string literal is an lvalue array of const char; overloads on const char(&)[N] vs const char* and template deduction of N.
#include <cstdio>
template <int N> const char *arr(const char (&)[N]) { return "array"; }
const char *arr(const char *) { return "pointer"; }
template <int N> int len(const char (&)[N]) { return N - 1; }
int main() { const char *p = "x"; std::printf("%s %s %d\n", arr("abc"), arr(p), len("hello")); return 0; }
