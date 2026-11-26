#include <cstdio>
int main() { register int r = 3; static int s = 4; extern int ext; thread_local int t = 1; (void)t; std::printf("%d %d %d\n", r, s, ext); return 0; } int ext = 5;
