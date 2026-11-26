#include <cstdio>
template <class... Ts> struct Al { alignas(Ts...) char c; };
int main() { std::printf("%d\n", (int)alignof(Al<char, double>)); return 0; }
