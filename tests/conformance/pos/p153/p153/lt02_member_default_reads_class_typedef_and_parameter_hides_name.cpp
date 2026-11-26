#include <cstdio>
#include <string>
#include <type_traits>
template <class T> struct C {
    typedef T value_type;
    template <class P, class = typename std::enable_if<std::is_constructible<value_type, P &&>::value>::type>
    int f(P &&) { return 1; }
    int f(const value_type &) { return 2; }
};
int main() {
    C<std::string> c;
    std::string s;
    std::printf("%d %d\n", c.f("lit"), c.f(s));
}
