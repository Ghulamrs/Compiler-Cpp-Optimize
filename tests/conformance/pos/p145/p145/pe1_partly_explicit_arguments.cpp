// [temp.arg.explicit]/3: the trailing template arguments a call does not write are deduced from it - a
// pack among them - and a non-type one may be written while the type after it is deduced.
#include <cstdio>
#include <utility>
struct S { S(int a, char b) : v(a + b) {} int v; };
template <class T, class... A> T make(A &&... a) { return T(std::forward<A>(a)...); }
template <class T, class U> T conv(U u) { return T(u); }
template <int N, class T> T times(T t) { return t * N; }
template <class T> int which(T) { return 1; }
template <class T> int which(T *) { return 2; }
template <class T, class U> int pair2(T, U) { return (int)(sizeof(T) * 10 + sizeof(U)); }
int main() {
    S s = make<S>(1, 'a');
    int i = make<int>();
    std::printf("%d %d %g %d %g\n", s.v, i, conv<double>(3), times<3>(5), times<2>(1.25));
    std::printf("%d %d %d %d\n", which<int>(0), which<int>((int *)0), pair2<char>(1, 2.0), pair2<short, char>(1, 2));
    return 0;
}
