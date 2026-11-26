#include <cstdio>
template <class T> void may(T) noexcept(noexcept(T())); template <class T> void may(T) noexcept(noexcept(T())) {} struct Thr { Thr() noexcept(false) {} }; template <class T> char (&sz(T))[sizeof(T)]; template <class T> int val(T t, char (*)[sizeof(T) == 4] = 0) { return 4; } template <class T> int val(T t, char (*)[sizeof(T) == 8] = 0) { return 8; }
int main() { std::printf("%d %d %d %d %d\n", (int)noexcept(may(1)), (int)noexcept(may(Thr())), (int)sizeof(sz(1.0)), val(1), val(1.0)); return 0; }
