#include <cstdio>
struct W { int v; W() : v(40) {} W(int x) : v(x) {} };
template <class T> T pick(T a, T b = T(), int k = sizeof(T)) { return k > 4 ? b : a; }
template <class T, int N = 3> int scale(T a, T b = T(N)) { return (int)(a * b); }
template <class T> struct Box { T v; int put(T x = T(7)) { v = x; return (int)v; } };
int sumW(W a, W b = W()) { return a.v + b.v; }
template <class T> int sumT(T a, T b = T()) { return a.v + b.v; }
int main() { Box<long> bx; std::printf("%d %d %d %d %d %d %d\n", pick(2), (int)pick(2.5), scale(2), scale(2.0), bx.put(), sumW(W(1)), sumT(W(2))); return 0; }
