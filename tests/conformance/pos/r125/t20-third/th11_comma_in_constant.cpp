// A comma operator inside a constant expression is allowed in C++11 ([expr.const]/2 no longer lists it).
#include <cstdio>
int main() { int a[(1, 3)]; std::printf("%d\n", (int)(sizeof(a) / sizeof(int))); return 0; }
