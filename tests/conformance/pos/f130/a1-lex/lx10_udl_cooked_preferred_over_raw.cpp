#include <cstdio>
int operator"" _x(unsigned long long) { return 1; }
int operator"" _x(const char *) { return 2; }
int operator"" _y(const char *) { return 3; }
template <char... > int operator"" _z() { return 4; }
int main() { std::printf("%d %d %d\n", 12_x, 12_y, 12_z); return 0; }
