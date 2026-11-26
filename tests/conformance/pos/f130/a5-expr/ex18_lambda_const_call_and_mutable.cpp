#include <cstdio>
int main() { int n = 0; auto l = [n]() mutable { return ++n; }; const auto k = [n]() { return n + 10; }; l(); std::printf("%d %d %d\n", l(), k(), n); return 0; }
