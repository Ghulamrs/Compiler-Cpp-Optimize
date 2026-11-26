// [temp.local]/6: a template template parameter hides what its name means around it - the `A` of a template asking for
// this one, in or out of a namespace.
#include <cstdio>
#include <type_traits>
namespace s {
template <class A, class T> struct RB {};
template <template <class, class...> class A, class U, class... Args, class T>
struct RB<A<U, Args...>, T> { typedef A<T, Args...> type; };
template <class A, class T> struct W { typedef typename RB<A, T>::type type; };
}
template <class T> struct Tg {};
typedef s::W<Tg<int>, long>::type Y;
template <class T> struct Tg2 { template <class B> struct Z { typedef typename s::RB<Tg<T>, B>::type type; }; };
typedef Tg2<int>::Z<long>::type Y2;
int main() {
    std::printf("%d %d\n", (int)std::is_same<Y, Tg<long> >::value, (int)std::is_same<Y2, Tg<long> >::value);
    return 0;
}
