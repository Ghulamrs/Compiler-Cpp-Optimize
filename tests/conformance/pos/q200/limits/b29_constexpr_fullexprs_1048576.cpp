#include <cstdio>
constexpr int tree(int n) { return n == 0 ? 1 : tree(n - 1) + tree(n - 1); }
constexpr int leaves = tree(19);
static_assert(leaves == 524288, "");
int main() { std::printf("%d\n", leaves); return 0; }
