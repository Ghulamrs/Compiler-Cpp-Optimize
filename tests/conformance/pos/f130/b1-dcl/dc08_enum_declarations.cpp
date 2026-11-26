#include <cstdio>
enum A { a0, a1 = 5, a2 }; enum B : char { b0 = 'b' }; enum class C { c0, c1 }; enum class D : unsigned long long { d0 = 1ull << 40 }; enum struct E { e0 }; enum { anon0 = 2 };
enum F : int; void takes(F); enum F : int { f0 = 9 }; void takes(F f) { std::printf("%d ", (int)f); }
int main() { takes(f0); std::printf("%d %d %c %d %d %d %d %d %d\n", a0, a2, b0, (int)C::c1, (int)(D::d0 == static_cast<D>(1ull << 40)), (int)E::e0, anon0, (int)sizeof(A) >= 1, (int)sizeof(D)); return 0; }
