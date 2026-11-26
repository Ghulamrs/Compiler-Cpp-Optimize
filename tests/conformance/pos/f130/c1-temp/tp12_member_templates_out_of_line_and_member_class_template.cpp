#include <cstdio>
struct S { template <class T> T conv(T) const; template <class T> struct In { T v; }; template <class T> static T st(T); }; template <class T> T S::conv(T t) const { return t + 1; } template <class T> T S::st(T t) { return t * 2; }
template <class T> struct W { template <class U> U both(U) const; template <class U> struct N { U u; }; }; template <class T> template <class U> U W<T>::both(U u) const { return u + sizeof(T); }
int main() { S s; S::In<int> in{5}; W<char> w; W<char>::N<double> n{1.5}; std::printf("%d %d %d %d %g\n", s.conv(1), S::st(3), in.v, w.both(1), n.u); return 0; }
