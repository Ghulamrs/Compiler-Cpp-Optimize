#include <cstdio>
typedef int (*FP)(int); int twice(int x) { return x * 2; } struct Conv { operator FP() const { return twice; } };
int main() { Conv c; std::printf("%d\n", c(3)); return 0; }
