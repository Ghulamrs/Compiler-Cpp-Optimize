#include <cstdio>
#include <type_traits>
template <class T = int, class U = T> U conv(T t = T()) { return (U)t; } template <class T, class U = typename std::remove_const<T>::type> U strip(T t) { return t; } template <int N = 3, class T> int times(T t) { return N * (int)t; }
int main() { const int c = 2; std::printf("%d %g %d %d %d\n", conv(), conv<int, double>(3), strip(c), times(4), times<2>(5)); return 0; }
