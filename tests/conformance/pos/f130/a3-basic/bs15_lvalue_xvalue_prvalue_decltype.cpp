#include <cstdio>
#include <utility>
#include <type_traits>
struct S { int m; }; S make(); int &lr(); int &&rr();
int main() { S s; std::printf("%d %d %d %d %d %d\n", (int)std::is_same<decltype(s.m), int>::value, (int)std::is_same<decltype((s.m)), int&>::value, (int)std::is_same<decltype(std::move(s).m), int>::value, (int)std::is_same<decltype((std::move(s).m)), int&&>::value, (int)std::is_same<decltype(lr()), int&>::value, (int)std::is_same<decltype(rr()), int&&>::value); return 0; }
