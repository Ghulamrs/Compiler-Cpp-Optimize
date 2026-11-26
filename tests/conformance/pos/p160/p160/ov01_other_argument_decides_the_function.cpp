// [over.over] with [over.match.best]: each candidate takes the function its parameter wants from an overloaded
// name, and the other arguments choose among the candidates.
#include <cstdio>
int f(int x) { return x; }
double f(double x) { return x * 2; }
template <class T> T tw(T x) { return x + x; }
int apply(int (*fn)(int), int v) { return fn(v) + 100; }
double apply(double (*fn)(double), double v) { return fn(v) + 200; }
int both(int (*a)(int), double (*b)(double), int v) { return a(v) + (int)b(v); }
int both(double (*a)(double), int (*b)(int), double v) { return (int)a(v) + b(1) + 1000; }
int main() {
    std::printf("%d %g\n", apply(f, 4), apply(f, 4.0));
    std::printf("%d %g\n", apply(tw, 9), apply(tw, 1.5));
    std::printf("%d %d\n", both(f, f, 3), both(f, f, 3.0));
    return 0;
}
