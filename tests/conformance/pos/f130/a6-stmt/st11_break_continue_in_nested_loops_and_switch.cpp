#include <cstdio>
int main() { for (int i = 0; i < 3; i++) { switch (i) { case 1: continue; default: break; } for (int j = 0; j < 3; j++) { if (j == 2) break; std::printf("%d%d", i, j); } } std::printf("\n"); return 0; }
