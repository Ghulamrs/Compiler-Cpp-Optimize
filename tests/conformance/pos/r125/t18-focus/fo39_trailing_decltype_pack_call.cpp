// Trailing return type with decltype of a call that expands a pack.
#include <cstdio>
#include <utility>
int add3(int a, int b, int c) { return a + b + c; }
template <class F, class... A> auto call(F f, A &&... a) -> decltype(f(std::forward<A>(a)...)) { return f(std::forward<A>(a)...); }
int main() { std::printf("%d\n", call(add3, 1, 2, 3)); return 0; }
