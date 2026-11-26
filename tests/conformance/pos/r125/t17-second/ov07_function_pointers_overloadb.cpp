// [over.over]: address of an overloaded function chosen by target type; static_cast to select; template function address; member function pointer to overloaded member; function reference params.
#include <cstdio>
int f(int x) { return x; }
double f(double x) { return x * 2; }
template <class T> T tw(T x) { return x + x; }
struct S { int m(int x) { return x; } int m(int x, int y) { return x + y; } };
int apply(int (*fn)(int), int v) { return fn(v); }
double apply(double (*fn)(double), double v) { return fn(v); }
int main() {
    int (*pi)(int) = f; double (*pd)(double) = f;
    std::printf("%d %g\n", pi(3), pd(3));
    std::printf("%g\n", apply(static_cast<double (*)(double)>(f), 4.0));
    int (*pt)(int) = tw; double (*pt2)(double) = &tw<double>;
    std::printf("%d %g\n", pt(5), pt2(1.5));
    int (S::*m1)(int) = &S::m; int (S::*m2)(int, int) = &S::m; S s;
    std::printf("%d %d\n", (s.*m1)(6), (s.*m2)(6, 7));
    int (&rf)(int) = f; std::printf("%d\n", rf(8));
    return 0;
}
