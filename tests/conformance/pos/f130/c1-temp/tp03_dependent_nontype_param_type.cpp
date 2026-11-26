#include <cstdio>
template <class T, T V> struct C { static const T value = V; }; template <class T, T *P> int viaptr() { return *P; } int gv = 9; template <int N, int M = N * 2> struct D { static const int v = M; }; template <class T, int N = sizeof(T)> struct Sz { static const int v = N; };
int main() { std::printf("%d %d %d %d %d\n", C<int, 7>::value, (int)C<char, 'q'>::value, viaptr<int, &gv>(), D<4>::v, Sz<double>::v); return 0; }
