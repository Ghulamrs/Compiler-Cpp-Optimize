// [expr.call]/7, [support.runtime]: a class with no non-trivial copy or destructor goes through `...`, small and large,
// among scalars, and va_arg gives each back by its own type.
#include <cstdarg>
#include <cstdio>
struct Small { int a; };
struct Two { long long a, b; };
struct Three { int a, b, c; };
struct Large { int a[6]; };
struct Doubles { double x, y; };
int sum(int n, ...) {
    va_list ap;
    va_start(ap, n);
    int total = 0;
    Small s = va_arg(ap, Small);
    total += s.a;
    total += va_arg(ap, int);
    Two t = va_arg(ap, Two);
    total += static_cast<int>(t.a + t.b);
    double d = va_arg(ap, double);
    total += static_cast<int>(d);
    Large l = va_arg(ap, Large);
    for (int i = 0; i < 6; i++) total += l.a[i];
    Three h = va_arg(ap, Three);
    total += h.a + h.b + h.c;
    Doubles f = va_arg(ap, Doubles);
    total += static_cast<int>(f.x + f.y);
    total += va_arg(ap, int);
    va_end(ap);
    return total + n;
}
const char *which(...) { return "other"; }
const char *which(int) { return "int"; }
Large makeLarge(int v) {
    Large l;
    for (int i = 0; i < 6; i++) l.a[i] = v + i;
    return l;
}
int main() {
    Small s = { 1 };
    Two t = { 2, 3 };
    Large l = makeLarge(10);
    Three h = { 4, 5, 6 };
    Doubles f = { 1.5, 2.5 };
    std::printf("%d\n", sum(100, s, 7, t, 8.5, l, h, f, 9));
    std::printf("%d\n", sum(0, s, 0, t, 0.0, makeLarge(0), h, f, 0));
    std::printf("%s %s\n", which(s), which(1));
    return 0;
}
