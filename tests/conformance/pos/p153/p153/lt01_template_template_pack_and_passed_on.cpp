#include <cstdio>
template <class T> struct One { int v; };
template <class T, class U = int> struct Two { int v; };
template <class X, class T> struct R { typedef void type; };
template <template <class, class...> class A, class U, class... Args, class T> struct R<A<U, Args...>, T> { typedef A<T, Args...> type; };
template <class A> struct Id { typedef A type; };
template <template <class> class Trait, class A> struct Apply { typedef typename Trait<A>::type type; };
template <template <class> class Trait, class... A> struct Any { static const int value = 0; };
template <template <class> class Trait, class A, class... Rest> struct Any<Trait, A, Rest...> {
    static const int value = sizeof(typename Trait<A>::type) + Any<Trait, Rest...>::value;
};
int main() {
    R<One<int>, char>::type a;
    R<Two<int, long>, char>::type b;
    Apply<Id, double>::type d = 2.5;
    std::printf("%d %d %d %d\n", (int)sizeof(a), (int)sizeof(b), (int)d, Any<Id, char, int>::value);
}
