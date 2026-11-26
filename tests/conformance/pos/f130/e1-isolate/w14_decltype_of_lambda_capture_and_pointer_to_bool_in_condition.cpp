#include <cstdio>
int main() { int x = 1; int *p = &x; bool a = p; bool b(p); bool c = {p != nullptr}; bool d = static_cast<bool>(p); struct S { int m; }; int S::*pm = &S::m; bool e = pm; bool f = !pm; std::printf("%d %d %d %d %d %d\n", (int)a, (int)b, (int)c, (int)d, (int)e, (int)f); return 0; }
