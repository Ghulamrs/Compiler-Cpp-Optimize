#include <cstdio>
struct B { unsigned a : 3; int b : 4; unsigned long long c : 40; }; enum E { e1 = 1 }; enum U : unsigned { u1 = 1 }; enum class SC { s = 1 };
template <class T> int k(T) { return 0; } template <> int k(int) { return 1; } template <> int k(unsigned) { return 2; } template <> int k(unsigned long long) { return 3; } template <> int k(E) { return 4; } template <> int k(SC) { return 5; }
int main() { B b{7, 3, 1}; std::printf("%d %d %d %d %d %d %d\n", k(b.a + 0), k(b.b + 0), k(b.c + 0), k(e1 + 0), k(u1 + 0), k(e1), k(SC::s)); std::printf("%d\n", b.a + b.b); return 0; }
