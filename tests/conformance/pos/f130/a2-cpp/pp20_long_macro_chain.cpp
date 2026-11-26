#include <cstdio>
#define A0 1
#define A1 A0 + A0
#define A2 A1 + A1
#define A3 A2 + A2
#define A4 A3 + A3
#define A5 A4 + A4
#define A6 A5 + A5
#define A7 A6 + A6
#define A8 A7 + A7
int main() { std::printf("%d\n", A8); return 0; }
