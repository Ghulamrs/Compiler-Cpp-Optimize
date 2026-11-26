// [temp.deduct.partial]/9: equal deductions either way, an lvalue reference is more specialized than an rvalue
// one; and reference binding has already decided where the conversions differ.
#include <cstdio>
template <class T> const char *k(const T &) { return "const T&"; }
template <class T> const char *k(T &&) { return "T&&"; }
template <class T> const char *m(T &) { return "T&"; }
template <class T> const char *m(T &&) { return "T&&"; }
int main() {
    int i = 0;
    const int ci = 0;
    std::printf("%s %s %s\n", k(i), k(ci), k(1));
    std::printf("%s %s %s\n", m(i), m(ci), m(1));
    return 0;
}
