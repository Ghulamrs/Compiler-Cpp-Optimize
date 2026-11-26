#include <cstdio>
template <class T> int val(T, char (*)[sizeof(T) == 4] = 0) { return 4; } template <class T> int val(T, char (*)[sizeof(T) == 8] = 0) { return 8; }
int main() { std::printf("%d %d\n", val(1), val(1.0)); return 0; }
