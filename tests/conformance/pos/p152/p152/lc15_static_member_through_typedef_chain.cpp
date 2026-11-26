#include <cstdio>
#include <type_traits>
template <class A> struct D1 { typedef std::false_type type; };
template <class A, bool = true> struct D2 { typedef std::false_type type; };
template <class A> struct D2<A, false> { typedef std::true_type type; };
template <class A> struct W { typedef typename D2<A>::type flag; };
int main() {
    std::printf("%d\n", (int)D1<int>::type::value);
    std::printf("%d\n", (int)D2<int>::type::value);
    std::printf("%d\n", (int)W<int>::flag::value);
    typedef W<int> WI;
    std::printf("%d\n", (int)WI::flag::value);
}
