#include <cstdio>
struct stat { int x; }; int stat(int v) { return v + 1; }
int main() { struct stat s; s.x = 1; std::printf("%d %d\n", s.x, stat(1)); return 0; }
