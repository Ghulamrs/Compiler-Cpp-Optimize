#include <cstdio>
struct S { int v = 1; int f() & { return 1; } int f() && { return 2; } int g() const & { return 3; } int g() const && { return 4; } int h() const volatile { return 5; } };
int main() { S s; const S cs; std::printf("%d %d %d %d %d\n", s.f(), S().f(), cs.g(), static_cast<const S&&>(s).g(), s.h()); return 0; }
