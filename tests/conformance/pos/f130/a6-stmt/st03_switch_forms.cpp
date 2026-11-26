#include <cstdio>
enum class E { a, b }; enum F { f1 = 1, f2 };
int main() { E e = E::b; switch (e) { case E::a: std::printf("a"); break; case E::b: std::printf("b"); break; } char16_t c = u'x'; switch (c) { case u'x': std::printf("x"); break; default: break; }
int n = 3; switch (n) { case 1: case 2: std::printf("12"); break; case 3: { int k = 5; std::printf("%d", k); } default: std::printf("d"); } switch (f2) { case f1: break; case f2: std::printf("f2"); } long long big = 1LL << 40; switch (big) { case 1LL << 40: std::printf("big"); break; } std::printf("\n"); return 0; }
