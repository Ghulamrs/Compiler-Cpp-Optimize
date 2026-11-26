// [temp.class.spec.match]: partial specializations on an array's bound, an array of unknown bound, a
// function type and one with a parameter pack, a pointer to member; [temp.arg.type]: a function type as a
// template argument - what std::function is written over.
#include <cstdio>
template <class T> struct K { static const int v = 0; };
template <class T, int N> struct K<T[N]> { static const int v = 10 + N; };
template <class T> struct K<T[]> { static const int v = 3; };
template <class R, class A> struct K<R(A)> { static const int v = 4; };
template <class R, class... As> struct K<R(As...)> { static const int v = 50 + sizeof...(As); };
template <class C, class M> struct K<M C::*> { static const int v = 8; };
struct S { int m; };
template <class F> struct Fn;
template <class R, class... A> struct Fn<R(A...)> {
    R (*p)(A...);
    R operator()(A... a) const { return p(a...); }
    static const int arity = sizeof...(A);
};
int twice(int x) { return 2 * x; }
long sum3(int a, short b, char c) { return a + b + c; }
template <class T> int count(const T &) { return K<T>::v; }
int main() {
    int arr[4] = {0};
    Fn<int(int)> f = { twice };
    Fn<long(int, short, char)> g = { sum3 };
    std::printf("%d %d %d %d %d %d %d\n", K<int>::v, K<int[3]>::v, K<int[]>::v, K<int(char)>::v,
                K<int(char, char, char)>::v, K<int S::*>::v, count(arr));
    std::printf("%d %d %ld %d\n", f(21), Fn<int(int)>::arity, g(1, 2, 3), Fn<long(int, short, char)>::arity);
    return 0;
}
