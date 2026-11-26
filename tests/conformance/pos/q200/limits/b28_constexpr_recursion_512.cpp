#include <cstdio>
constexpr int depth(int n) { return n == 0 ? 0 : depth(n - 1) + 1; }
static_assert(depth(512) == 512, "");
constexpr int d = depth(512);
int main() { std::printf("%d\n", d); return 0; }
