#include <cstdio>
#include <vector>
struct P { int a, b; }; P mk() { return {1, 2}; } std::vector<int> mv() { return {3, 4}; } void nothing() { return; } void nothing2() { return nothing(); }
int main() { P p = mk(); std::vector<int> v = mv(); nothing2(); std::printf("%d %d %d\n", p.a + p.b, v[0], v[1]); return 0; }
