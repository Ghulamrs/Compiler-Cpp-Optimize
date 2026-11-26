// [temp.class.spec.match]: a partial specialization whose argument is a template-id with fixed members and a pack, Tup<H, T...>.
#include <cstdio>
template <class... Ts> struct Tup;
template <> struct Tup<> { static const int n = 0; };
template <class H, class... T> struct Tup<H, T...> { H h; Tup<T...> t; static const int n = 1 + Tup<T...>::n; };
template <int I, class Tp> struct Get;
template <class H, class... T> struct Get<0, Tup<H, T...> > { static H &get(Tup<H, T...> &t) { return t.h; } };
template <int I, class H, class... T> struct Get<I, Tup<H, T...> > {
    static auto get(Tup<H, T...> &t) -> decltype(Get<I - 1, Tup<T...> >::get(t.t)) { return Get<I - 1, Tup<T...> >::get(t.t); }
};
template <class T> struct Rest { static const int n = -1; };
template <class H, class... T> struct Rest<Tup<H, T...> > { static const int n = (int)sizeof...(T); };
template <class A, class B> struct Both { static const int v = 0; };
template <class... X, class... Y> struct Both<Tup<X...>, Tup<Y...> > { static const int v = (int)sizeof...(X) * 10 + (int)sizeof...(Y); };
int main() {
    Tup<int, char, double> t;
    Get<0, Tup<int, char, double> >::get(t) = 3;
    Get<1, Tup<int, char, double> >::get(t) = 'q';
    Get<2, Tup<int, char, double> >::get(t) = 1.5;
    std::printf("%d %d %c %g\n", Tup<int, char, double>::n, t.h, t.t.h, t.t.t.h);
    std::printf("%d %d %d %d\n", Rest<Tup<int> >::n, Rest<Tup<int, int, int> >::n, Rest<int>::n,
                Both<Tup<int, int>, Tup<char> >::v);
    return 0;
}
