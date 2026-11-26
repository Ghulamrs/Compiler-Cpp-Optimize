#include <cstdio>
#include <cstddef>
struct alignas(double) A { char c; }; struct alignas(8) alignas(4) B { char c; }; struct C { alignas(16) char x; char y; }; template <class T> struct alignas(T) D { char c; };
int main() { alignas(64) static char buf[3]; alignas(int) char loc[5]; std::printf("%d %d %d %d %d %d %d\n", (int)alignof(A), (int)alignof(B), (int)alignof(C), (int)sizeof(C), (int)alignof(D<long long>), (int)((std::size_t)buf % 64), (int)((std::size_t)loc % 4)); return 0; }
