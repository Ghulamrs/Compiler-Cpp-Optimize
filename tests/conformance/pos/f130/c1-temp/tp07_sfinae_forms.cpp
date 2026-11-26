#include <cstdio>
#include <type_traits>
#include <utility>
template <class T, class = typename std::enable_if<std::is_integral<T>::value>::type> int kind(T) { return 1; } template <class T, class = typename std::enable_if<std::is_floating_point<T>::value>::type, class = void> int kind(T) { return 2; }
template <class T> auto has_size(const T &t) -> decltype(t.size(), 1) { return 1; } int has_size(...) { return 0; } struct WithSize { int size() const { return 0; } };
template <class T> typename T::value_type first(const T &t) { return *t.begin(); } template <class T> T first(T t) { return t; }
template <class T, typename std::enable_if<sizeof(T) == 1, int>::type = 0> int small(T) { return 1; } template <class T, typename std::enable_if<sizeof(T) != 1, int>::type = 0> int small(T) { return 0; }
template <class T> struct is_int { static const bool v = false; }; template <> struct is_int<int> { static const bool v = true; };
int main() { WithSize w; std::printf("%d %d %d %d %d %d %d %d\n", kind(1), kind(1.5), has_size(w), has_size(1), first(5), small('c'), small(1), (int)is_int<int>::v); return 0; }
