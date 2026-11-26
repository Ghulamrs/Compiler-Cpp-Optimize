// [temp.deduct]/8: substitution failure is not an error: enable_if on return/param/template param, nested type absence, sizeof trick, expression SFINAE in trailing return (C++11), void_t-like detection.
#include <cstdio>
#include <type_traits>
#include <vector>
template <class T, class = typename std::enable_if<std::is_integral<T>::value>::type> const char *kind(T, int) { return "integral"; }
template <class T, class = typename std::enable_if<std::is_floating_point<T>::value>::type> const char *kind(T, long) { return "floating"; }
template <class T, class = typename std::enable_if<std::is_pointer<T>::value>::type> const char *kind(T, char) { return "pointer"; }
template <class T, class = typename std::enable_if<std::is_integral<T>::value>::type> const char *cls(const T &) { return "integral"; }
const char *cls(...) { return "other"; }
template <class T> struct HasValueType { typedef char yes[1]; typedef char no[2]; template <class U> static yes &test(typename U::value_type *); template <class U> static no &test(...); static const bool v = sizeof(test<T>(0)) == sizeof(yes); };
template <class T> auto hasSize(const T &t) -> decltype(t.size(), (const char *)0) { return "has size()"; }
template <class T> const char *hasSize(...) { return "no size()"; }
template <class T> auto addable(const T &a, const T &b) -> decltype(a + b) { return a + b; }
struct NoPlus {};
template <class T> auto tryPlus(const T &a, int) -> decltype(a + a, true) { return true; }
template <class T> bool tryPlus(const T &, long) { return false; }
struct Cls {};
int main() {
    int *p = 0;
    std::printf("%s %s %s\n", kind(1, 0), kind(1.5, 0L), kind(p, 'c'));
    std::printf("%s %s\n", cls(5), cls(Cls()));
    std::printf("%d %d\n", HasValueType<std::vector<int> >::v, HasValueType<int>::v);
    std::vector<int> v; std::printf("%s %s\n", hasSize(v), hasSize<int>(0));
    std::printf("%d %g\n", addable(1, 2), addable(1.5, 2.0));
    std::printf("%d %d\n", tryPlus(1, 0), tryPlus(NoPlus(), 0));
    return 0;
}
