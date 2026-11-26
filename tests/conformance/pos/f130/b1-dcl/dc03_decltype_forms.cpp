#include <cstdio>
#include <type_traits>
struct S { int m; double f() const; }; int &&rv(); const S cs{1};
int main() { S s; decltype(s.m) a = 1; decltype((s)) r = s; decltype(cs.m) b = 2; decltype(rv()) c = 3; decltype(s.f()) d = 1.5; decltype(S::m) e = 4;
std::printf("%d %d %d %d %d %d %d\n", a, r.m = 9, b, c, (int)d, e, (int)std::is_same<decltype(cs.m), int>::value); std::printf("%d %d\n", (int)std::is_same<decltype((cs.m)), const int&>::value, (int)std::is_same<decltype(S::m), int>::value); return 0; }
