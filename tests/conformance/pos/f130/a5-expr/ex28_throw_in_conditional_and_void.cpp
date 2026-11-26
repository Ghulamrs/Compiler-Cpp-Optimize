#include <cstdio>
int main() { bool c = true; int v = c ? 1 : throw 2; try { int w = !c ? 3 : throw 4; (void)w; } catch (int e) { std::printf("%d %d\n", v, e); } return 0; }
