// Within one TU an inline function's static local is one object (the cross-TU case is in the ABI suite).
#include <cstdio>
inline int counter() { static int n = 0; return ++n; }
int a() { return counter(); }
int b() { return counter(); }
int main() { int x = a(); int y = b(); int z = counter(); std::printf("%d %d %d\n", x, y, z); return 0; }
