#include <cstdio>
static char big[262144];
int main() { big[262143] = 1; std::printf("%d %d\n", (int)sizeof big, big[262143]); return 0; }
