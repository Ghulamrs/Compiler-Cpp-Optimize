#include <cstdio>
#include <type_traits>
struct S { int m; }; const S cs{1}; S s{2}; const S &cr = s;
int main() { std::printf("%d %d %d %d\n", (int)std::is_same<decltype(cs.m), int>::value, (int)std::is_same<decltype(cr.m), int>::value, (int)std::is_same<decltype((cs.m)), const int &>::value, (int)std::is_same<decltype(s.m), int>::value); return 0; }
