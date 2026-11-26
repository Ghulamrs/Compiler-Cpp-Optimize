// [temp.mem]/1: a member class template of a variadic class template, named in an expression, its owner's pack in its body.
#include <cstdio>
template <class... Ts> struct Cnt { static const int v = (int)sizeof...(Ts); };
template <class... Ts> struct P {
    template <class... Us> struct W { static const int v = Cnt<Ts...>::v * 10 + Cnt<Us...>::v; int x; W(int a) : x(a * v) {} };
    template <class U> using Add = P<Ts..., U>;
};
template <class T> struct Q { template <class U> struct W { static int size() { return (int)sizeof(U) + (int)sizeof(T); } }; };
template <class T> struct Arity;
template <class... Ts> struct Arity<P<Ts...> > { static const int v = (int)sizeof...(Ts); };
int main() {
    std::printf("%d %d %d\n", P<int, char>::W<double>::v, P<>::W<int, int, int>::v, P<int>::W<>::v);
    std::printf("%d %d %d\n", P<int, char>::W<long>(2).x, Q<char>::W<int>::size(), Arity<P<int>::Add<char> >::v);
    std::printf("%d\n", P<int, int>::Add<char>::W<>::v);
    return 0;
}
