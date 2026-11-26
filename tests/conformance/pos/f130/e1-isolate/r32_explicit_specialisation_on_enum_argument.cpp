#include <cstdio>
enum E { e1 = 1 }; template <class T> int k(T) { return 0; } template <> int k(int) { return 1; } template <> int k(E) { return 2; }
int main() { std::printf("%d %d\n", k(1), k(e1)); return 0; }
