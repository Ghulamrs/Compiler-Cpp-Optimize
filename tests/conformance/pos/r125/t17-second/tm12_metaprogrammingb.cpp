// Classic metaprogramming: integral_constant, conditional, is_same, type lists, compile-time recursion depth 64, constexpr+templates mix, decltype in templates, index sequence hand-rolled.
#include <cstdio>
#include <type_traits>
template <class... Ts> struct List { static const int size = sizeof...(Ts); };
template <class L, class T> struct Push;
template <class T, class... Ts> struct Push<List<Ts...>, T> { typedef List<Ts..., T> type; };
template <class L> struct Head;
template <class H, class... T> struct Head<List<H, T...> > { typedef H type; };
template <int N> struct Depth { static const int v = Depth<N - 1>::v + 1; };
template <> struct Depth<0> { static const int v = 0; };
template <int... Is> struct Seq {};
template <int N, int... Is> struct MakeSeq : MakeSeq<N - 1, N - 1, Is...> {};
template <int... Is> struct MakeSeq<0, Is...> { typedef Seq<Is...> type; };
template <int... Is> int sumSeq(Seq<Is...>) { int arr[] = {Is..., 0}; int s = 0; for (int x : arr) s += x; return s; }
template <class T> struct IsPtrLike : std::integral_constant<bool, std::is_pointer<T>::value> {};
template <bool C, class T, class F> struct Cond { typedef T type; };
template <class T, class F> struct Cond<false, T, F> { typedef F type; };
template <class T> typename std::conditional<(sizeof(T) > 4), long long, int>::type widen(T t) { return t; }
constexpr int fib(int n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }
template <int N> struct FibT { static const int v = fib(N); };
int main() {
    typedef Push<Push<List<>, int>::type, double>::type L2;
    std::printf("%d %d %d\n", L2::size, std::is_same<Head<L2>::type, int>::value, List<>::size);
    std::printf("%d\n", Depth<64>::v);
    std::printf("%d\n", sumSeq(MakeSeq<5>::type()));
    std::printf("%d %d\n", IsPtrLike<int *>::value, IsPtrLike<int>::value);
    std::printf("%d %d\n", (int)sizeof(Cond<true, char, double>::type), (int)sizeof(Cond<false, char, double>::type));
    std::printf("%d %d\n", (int)sizeof(widen(1)), (int)sizeof(widen(1.0)));
    std::printf("%d %d\n", FibT<20>::v, std::is_same<decltype(fib(1)), int>::value);
    std::printf("%d %d %d\n", std::is_same<std::remove_cv<const volatile int>::type, int>::value, std::is_const<const int>::value, std::is_reference<int &>::value);
    return 0;
}
