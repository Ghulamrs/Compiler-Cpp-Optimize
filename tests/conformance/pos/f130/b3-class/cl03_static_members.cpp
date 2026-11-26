#include <cstdio>
struct S { static int count; static const int k = 3; static constexpr float f = 1.5f; static S *head; static int get() { return count; } static S self; int v; }; int S::count = 1; S *S::head = nullptr; S S::self{4}; constexpr float S::f;
int main() { S::count++; S s{2}; S::head = &s; std::printf("%d %d %g %d %d %d\n", S::get(), s.k, S::f, S::head->v, S::self.v, (int)sizeof(S)); const float *pf = &S::f; (void)pf; return 0; }
