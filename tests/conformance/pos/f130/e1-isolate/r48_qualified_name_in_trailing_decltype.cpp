#include <cstdio>
struct K { static int get(int x) { return x + 1; } }; template <class T> auto f(T t) -> decltype(K::get(t)) { return K::get(t); }
int main() { std::printf("%d\n", f(1)); return 0; }
