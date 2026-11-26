#include <cstdio>
#include <vector>
template <class T, class A> struct Al {}; template <class T> struct One { T t; }; template <class T, class U = int> struct TwoDef { T t; U u; };
template <template <class> class TT, class T> struct ApplyOne { TT<T> x; }; template <template <class, class> class TT, class T> struct ApplyTwo { TT<T, int> x; }; template <template <class...> class TT, class... Ts> struct ApplyAny { TT<Ts...> x; }; template <template <class, class> class C = TwoDef> struct Def { C<int, int> c; };
int main() { ApplyOne<One, int> a; a.x.t = 1; ApplyTwo<Al, char> b; ApplyAny<One, char> c; c.x.t = 'c'; ApplyAny<TwoDef, int, char> d; d.x.u = 'u'; Def<> e; e.c.t = 2; std::printf("%d %c %c %d %d\n", a.x.t, c.x.t, d.x.u, e.c.t, (int)sizeof(b)); return 0; }
