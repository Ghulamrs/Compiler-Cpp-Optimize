#include <cstdio>
struct S { int v; explicit operator bool() const { return v != 0; } }; S get(int v) { return S{v}; }
int main() { for (int i = 0; S s = get(i < 2 ? 1 : 0); i++) std::printf("i"); std::printf("\n"); return 0; }
