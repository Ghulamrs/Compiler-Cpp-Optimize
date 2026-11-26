#include <cstdio>
template <class T> struct alignas(T) D { char c; };
int main() { std::printf("%d\n", (int)alignof(D<long long>)); return 0; }
