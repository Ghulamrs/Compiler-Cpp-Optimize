#include <cstdio>
int g = 11;
int main() { { extern int g; std::printf("%d\n", g); } int g = 2; std::printf("%d\n", g); return 0; }
