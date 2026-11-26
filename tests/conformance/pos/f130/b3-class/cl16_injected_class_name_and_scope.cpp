#include <cstdio>
struct S { int v; S(int v) : v(v) {} S twice() const { return S(v * 2); } static S make(int x) { return S(x); } struct S2 { S::S2 *self() { return this; } int w = 3; }; }; template <class T> struct W { W *next = nullptr; W<T> *me() { return this; } T t{}; W() {} W(T t) : t(t) {} W<int> conv() const { return W<int>((int)t); } };
int main() { S s = S::make(2).twice(); S::S2 s2; W<double> w(1.5); std::printf("%d %d %d %d\n", s.v, s2.self()->w, (int)(w.me() == &w), w.conv().t); return 0; }
