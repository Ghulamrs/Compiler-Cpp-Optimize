// [temp.class.spec]: partial specialisations for pointers, references, arrays, const, template-ids, two params with one fixed, non-type params; the most specialised wins; member access to specialisation members.
#include <cstdio>
template <class T> struct K { static const char *n() { return "T"; } };
template <class T> struct K<T *> { static const char *n() { return "T*"; } };
template <class T> struct K<const T *> { static const char *n() { return "const T*"; } };
template <class T> struct K<T &> { static const char *n() { return "T&"; } };
template <class T> struct K<T &&> { static const char *n() { return "T&&"; } };
template <class T, int N> struct K<T[N]> { static const char *n() { return "T[N]"; } };
template <class T> struct K<T[]> { static const char *n() { return "T[]"; } };
template <class T> struct K<const T> { static const char *n() { return "const T"; } };
template <class R, class A> struct K<R(A)> { static const char *n() { return "R(A)"; } };
template <class R, class A> struct K<R (*)(A)> { static const char *n() { return "R(*)(A)"; } };
template <class C, class M> struct K<M C::*> { static const char *n() { return "M C::*"; } };
template <class T, class U> struct P { static const char *n() { return "T,U"; } };
template <class T> struct P<T, T> { static const char *n() { return "T,T"; } };
template <class T> struct P<T, int> { static const char *n() { return "T,int"; } };
template <class U> struct P<int *, U> { static const char *n() { return "int*,U"; } };
template <int N> struct Fact { static const int v = N * Fact<N - 1>::v; };
template <> struct Fact<0> { static const int v = 1; };
template <class T, class U> struct Box {};
template <class T> struct K<Box<T, T> > { static const char *n() { return "Box<T,T>"; } };
struct S { int m; };
int main() {
    std::printf("%s %s %s %s %s\n", K<int>::n(), K<int *>::n(), K<const int *>::n(), K<int &>::n(), K<int &&>::n());
    std::printf("%s %s %s %s\n", K<int[3]>::n(), K<int[]>::n(), K<const int>::n(), K<int (*)(double)>::n());
    std::printf("%s %s %s\n", K<int(double)>::n(), K<int S::*>::n(), K<Box<char, char> >::n());
    std::printf("%s %s %s %s\n", P<char, double>::n(), P<char, char>::n(), P<char, int>::n(), P<int *, double>::n());
    std::printf("%d %d\n", Fact<5>::v, Fact<10>::v);
    std::printf("%s %s\n", K<const int *const>::n(), K<Box<int, char> >::n());
    return 0;
}
