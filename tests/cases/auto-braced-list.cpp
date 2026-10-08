// `auto x = {...}` deduces std::initializer_list<E> - [dcl.spec.auto]/6, each
// element deducing E as a function argument would; and a variable written with
// that type takes the braces directly, [dcl.init.list]/3.
#include <initializer_list>
extern "C" int printf(const char *, ...);

static int total(std::initializer_list<int> xs) {
    int s = 0;
    for (const int *p = xs.begin(); p != xs.end(); ++p) s += *p;
    return s;
}

int main() {
    auto a = {1, 2, 3};
    const auto b = {2.5, 3.5};
    std::initializer_list<int> c = {10, 20};
    std::initializer_list<int> d{7};
    const char *s = "xyz";
    auto e = {s, s + 1};
    double sb = 0;
    for (const double *p = b.begin(); p != b.end(); ++p) sb += *p;
    int t = total(a);
    printf("a %d %d\n", static_cast<int>(a.size()), t);
    printf("b %d %g\n", static_cast<int>(b.size()), sb);
    printf("c %d d %d\n", total(c), total(d));
    printf("e %s %s\n", e.begin()[0], e.begin()[1]);
    return 0;
}
