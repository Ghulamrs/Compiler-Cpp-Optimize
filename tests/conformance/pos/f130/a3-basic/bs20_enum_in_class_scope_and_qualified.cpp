#include <cstdio>
struct S { enum E { a = 1, b }; enum class F : char { x = 'x', y }; };
int main() { S::E e = S::b; S::F f = S::F::y; std::printf("%d %d %d %d\n", e, (int)S::a, (int)f, (int)sizeof(S::F)); return 0; }
