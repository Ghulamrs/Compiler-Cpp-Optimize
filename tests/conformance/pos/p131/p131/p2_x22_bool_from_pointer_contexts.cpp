#include <cstdio>
int main() { int x = 1; int *p = &x; bool a = p; bool b = p; bool c(p); bool d = static_cast<bool>(p); std::printf("%d %d %d %d\n", (int)a, (int)b, (int)c, (int)d); return 0; }
