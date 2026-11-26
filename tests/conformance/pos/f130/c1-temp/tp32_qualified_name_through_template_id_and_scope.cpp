#include <cstdio>
template <class T> struct S { static int s; typedef T type; struct In { static int v; }; static int f() { return 1; } }; template <class T> int S<T>::s = (int)sizeof(T); template <class T> int S<T>::In::v = 7;
int main() { S<int>::type x = S<int>::s; int y = S<char>::In::v; int z = S<double>::f(); typedef S<long long> SL; std::printf("%d %d %d %d\n", x, y, z, SL::s); return 0; }
