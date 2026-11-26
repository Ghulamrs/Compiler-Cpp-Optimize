#include <cstdio>
template <class T> struct One { T t; }; template <template <class...> class TT, class... Ts> struct ApplyAny { TT<Ts...> x; };
int main() { ApplyAny<One, char> c; c.x.t = 'c'; std::printf("%c\n", c.x.t); return 0; }
