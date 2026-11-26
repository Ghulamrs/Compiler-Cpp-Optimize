// R7: [temp.expl.spec]/1 - a member function and a static data member of one specialization written out.
#include <cstdio>
template <class T> struct S {
    int f() { return 1; }
    int f(int x) { return 10 + x; }
    int g() const { return f2(); }
    int f2() const { return 100; }
    static int s;
};
template <class T> int S<T>::s = 1;
template <> int S<int>::f() { return 2; }
template <> int S<int>::f2() const { return 200; }
template <> int S<int>::s = 2;
int main() {
    S<int> a; S<long> b; S<int> *pa = &a;
    std::printf("%d %d %d %d %d %d %d %d\n", a.f(), pa->f(1), b.f(), b.f(2), a.g(), b.g(), S<int>::s, S<long>::s);
    return 0;
}