// [temp.param]/2 and [temp.deduct]/8: `typename enable_if<C, int>::type = 0` is a non-type parameter whose type
// depends on the ones before it; where that type cannot be formed the candidate drops out.
#include <cstdio>
#include <type_traits>
template <class T, typename std::enable_if<sizeof(T) == 1, int>::type = 0> int small(T) { return 1; }
template <class T, typename std::enable_if<sizeof(T) != 1, int>::type = 0> int small(T) { return 0; }
template <class T, typename std::enable_if<std::is_integral<T>::value, int>::type N = 7> int seven(T) { return N; }
template <class T, typename std::enable_if<!std::is_integral<T>::value, int>::type N = 8> int seven(T) { return N; }
template <bool B, class T = void> struct EI {};
template <class T> struct EI<true, T> { typedef T type; };
template <class T, typename EI<true, T>::type V = 3> T three(T) { return V; }
int main() {
    std::printf("%d %d %d %d %d\n", small('c'), small(1), small(2.5), seven(1), seven(1.5));
    std::printf("%d %d %d\n", small<char, 0>('x'), seven<int, 5>(0), (int)three(4L));
    return 0;
}
