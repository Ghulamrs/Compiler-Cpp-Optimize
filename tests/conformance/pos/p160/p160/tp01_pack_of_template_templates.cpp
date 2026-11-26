// [temp.param]/15, [temp.variadic]: a pack of template template parameters - counted, expanded into a base list and
// an argument list, passed on, deduced.
#include <cstdio>
template <class T> struct A { int a() const { return 1; } };
template <class T> struct B { int b() const { return 2; } };
template <template <class> class... Ts> struct L { static const int n = sizeof...(Ts); };
template <class... U> struct Tup { static const int n = sizeof...(U); };
template <template <class> class... Ts> struct H : Ts<int>... {};
template <template <class> class... Ts> struct M { typedef Tup<Ts<int>...> type; };
template <template <class> class First, template <class> class... Rest> struct Head {
    typedef First<char> type;
    static const int rest = sizeof...(Rest);
};
template <template <class> class... Ts> struct Fwd { static const int n = L<Ts...>::n; };
template <template <class> class... Ts> int cnt(L<Ts...>) { return sizeof...(Ts); }
int main() {
    H<A, B> h;
    Head<A, B, A>::type v;
    L<A, B> l;
    std::printf("%d %d %d %d\n", L<A, B>::n, L<>::n, h.a(), h.b());
    std::printf("%d %d %d %d\n", M<A, B, A>::type::n, v.a(), Head<A, B, A>::rest, Fwd<A, B, B>::n);
    std::printf("%d\n", cnt(l));
    return 0;
}
