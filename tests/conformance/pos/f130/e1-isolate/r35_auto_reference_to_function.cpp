#include <cstdio>
void fn(int x) { std::printf("%d\n", x); }
int main() { auto &rf = fn; rf(3); return 0; }
