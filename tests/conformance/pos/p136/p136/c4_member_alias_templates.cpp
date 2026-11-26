// R12: [temp.alias] inside a class, named through it.
#include <cstdio>
struct S {
    template <class T> using M = T;
    template <class T> using P = T *;
};
int twice(S::M<int> v) { return v * 2; }
int main() {
    S::M<int> m = 4;
    int x = 7;
    S::P<int> p = &x;
    S::M<double> d = 2.5;
    std::printf("%d %d %d %g\n", m, *p, twice(5), d);
    return 0;
}