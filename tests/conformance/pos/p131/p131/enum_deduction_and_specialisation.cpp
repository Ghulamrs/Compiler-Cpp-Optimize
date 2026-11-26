#include <cstdio>
enum E { e1 = 1 }; template <class T> int k(T) { return (int)sizeof(T) + 100; } template <> int k(int) { return 1; }
int f(E) { return 3; } int f(int) { return 4; }
template <class T> int g(T) { return 10; } template <> int g(E) { return 11; }
int main() { E x = e1; std::printf("%d %d %d %d %d %d\n", k(e1), k(x), f(e1), f(x), g(e1), g(x)); return 0; }
