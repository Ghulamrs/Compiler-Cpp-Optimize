#include <cstdio>
#include <type_traits>
template <class T, class U = typename std::remove_const<T>::type> U strip(T t) { return t; }
int main() { const int c = 2; std::printf("%d\n", strip(c)); return 0; }
