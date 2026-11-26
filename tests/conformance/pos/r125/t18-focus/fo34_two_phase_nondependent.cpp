// [temp.res]/9: a non-dependent name is bound at definition; a later overload must not be picked up at instantiation.
#include <cstdio>
void g(double) { std::printf("g(double) "); }
template <class T> void call(T t) { g(1); g(t); }
void g(int) { std::printf("g(int) "); }
int main() { call(1); std::printf("\n"); return 0; }
