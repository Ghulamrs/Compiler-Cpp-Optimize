#include <cstdio>
#include <cstddef>
template <class... Ts> struct Tuple {}; template <class T, class... Rest> struct Tuple<T, Rest...> : Tuple<Rest...> { T head; };
template <class... Ts> int count(Ts... ts) { int a[] = {0, ((void)ts, 1)...}; return (int)sizeof a / (int)sizeof a[0] - 1; } template <class... Ts> int sum(Ts... ts) { int a[] = {ts...}; int s = 0; for (int i = 0; i < (int)sizeof...(ts); i++) s += a[i]; return s; }
template <class... Ts> struct Sizes { static const std::size_t v[sizeof...(Ts) + 1]; }; template <class... Ts> const std::size_t Sizes<Ts...>::v[sizeof...(Ts) + 1] = {sizeof(Ts)..., 0};
template <int... Ns> struct Seq { static const int n = sizeof...(Ns); }; template <class... Ts> void each(Ts... ts) { int d[] = {(std::printf("%d ", (int)ts), 0)...}; (void)d; }
template <class... Bs> struct Multi : Bs... { Multi() : Bs()... {} };
struct A { int a = 1; }; struct B { int b = 2; };
int main() { Tuple<int, char, double> t; t.head = 1; Multi<A, B> m; each(1, 2.5, 'c'); std::printf("%d %d %d %d %d %d\n", count(1, 2, 3), sum(1, 2, 3), (int)Sizes<char, int, double>::v[2], Seq<1, 2, 3>::n, m.a + m.b, (int)sizeof(Tuple<>)); return 0; }
