#include <cstdio>
template <class T, class U = T, int N = 2> struct C { T t[N]; U u; static int count; C() : u() { count++; } friend int peek(const C &c) { return c.u + N; } }; template <class T, class U, int N> int C<T, U, N>::count = 0;
template <class T> struct Outer { struct Inner { T v; }; typedef Inner In; enum E { K = sizeof(T) }; static const int k = K; template <class U> friend struct Outer; }; template <template <class> class TT> struct Apply { TT<int> x; };
int main() { C<int> a; C<int, char, 3> b; C<int> c; Outer<double>::In in{2.5}; Apply<Outer> ap; ap.x.k; std::printf("%d %d %d %d %g %d %d\n", C<int>::count, C<int, char, 3>::count, peek(a), peek(b), in.v, (int)Outer<double>::K, (int)Outer<char>::k); (void)c; return 0; }
