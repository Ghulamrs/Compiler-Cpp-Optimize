// [over.over]/2 and [temp.deduct.funcaddr]: a function template named without a call, its specialization picked by the type wanted.
#include <cstdio>
template <class T> T twice(T t) { return t + t; }
int twice(int t) { return t * 100; }
template <class T> T neg(T t) { return -t; }
int apply(int (*f)(int), int v) { return f(v); }
double applyD(double (*f)(double), double v) { return f(v); }
template <class F> int run(F f) { return f(3); }
int main() {
    int (*pi)(int) = twice;
    double (*pd)(double) = twice;
    long (*pl)(long) = &neg<long>;
    std::printf("%d %g %ld\n", pi(2), pd(1.5), pl(4));
    std::printf("%d %g %d %d\n", apply(neg, 5), applyD(twice, 2.5), apply(twice<int>, 1), run(neg<int>));
    return 0;
}
