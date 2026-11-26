// A member template called with explicit arguments must be instantiated (a link error means it was referenced and never emitted).
#include <cstdio>
struct S { template <class T> const char *m(T) { return "m<T>"; } const char *m(int) { return "m(int)"; } template <class T> static int sz() { return (int)sizeof(T); } };
template <class T> int free() { return (int)sizeof(T) * 2; }
int main() { S s; std::printf("%s %s %d %d\n", s.m<int>(1), s.m(1), S::sz<double>(), free<char>()); return 0; }
