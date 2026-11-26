#include <cstdio>
struct S { template <class T> using M = T; };
int main() { S::M<int> m = 4; std::printf("%d\n", m); return 0; }
