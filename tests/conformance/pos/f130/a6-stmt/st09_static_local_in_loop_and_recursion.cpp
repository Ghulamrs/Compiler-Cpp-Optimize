#include <cstdio>
int calls = 0; int rec(int n) { static int s = (calls++, n); return n == 0 ? s : rec(n - 1); }
int main() { for (int i = 0; i < 3; i++) { static int k = i * 10; std::printf("%d", k); } std::printf(" %d %d\n", rec(5), calls); return 0; }
