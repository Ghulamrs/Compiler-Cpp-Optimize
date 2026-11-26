#include <cstdio>
#include <type_traits>
template <class T, typename std::enable_if<std::is_integral<T>::value, int>::type = 0> int k(T) { return 1; }
template <class T, typename std::enable_if<!std::is_integral<T>::value, int>::type = 0> int k(T) { return 2; }
int main() { std::printf("%d %d\n", k(1), k(1.5)); return 0; }
