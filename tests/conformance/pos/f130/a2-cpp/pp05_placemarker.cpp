#include <cstdio>
#define J(a, b) a ## b
#define K(a) J(a, )
#define L2(a, b) a b
int main() { int K(v) = 3; std::printf("%d %d\n", v, L2(,4)); return 0; }
