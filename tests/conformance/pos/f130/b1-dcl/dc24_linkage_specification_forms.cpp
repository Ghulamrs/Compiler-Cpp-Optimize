#include <cstdio>
extern "C" { int c1(void); int c1(void) { return 1; } }
extern "C" int c2() { return 2; }
extern "C++" int c3() { return 3; }
extern "C" { extern "C++" int c4() { return 4; } }
extern "C" typedef void (*cb_t)(int);
extern "C" int c1(void);
int main() { std::printf("%d %d %d %d %d\n", c1(), c2(), c3(), c4(), (int)sizeof(cb_t)); return 0; }
