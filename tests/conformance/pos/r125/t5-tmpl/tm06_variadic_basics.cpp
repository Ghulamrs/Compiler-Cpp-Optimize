// [temp.variadic]: sizeof..., recursive unpacking, pack expansion into function args, into initializer lists, into base lists, into template-ids; empty packs; packs of mixed types.
#include <cstdio>
#include <initializer_list>
template <class... Ts> int count(Ts...) { return (int)sizeof...(Ts); }
int sum() { return 0; }
template <class T, class... R> int sum(T t, R... r) { return (int)t + sum(r...); }
template <class... Ts> int sumList(Ts... ts) { int tot = 0; for (int x : {(int)ts...}) tot += x; return tot; }
template <class... Ts> struct Tuple;
template <> struct Tuple<> { static const int size = 0; };
template <class H, class... T> struct Tuple<H, T...> { H head; Tuple<T...> tail; static const int size = 1 + Tuple<T...>::size; };
template <class... Bs> struct Multi : Bs... { int all() const { return sumBases(static_cast<const Bs &>(*this)...); } static int sumBases() { return 0; } template <class B, class... R> static int sumBases(const B &b, const R &... r) { return b.v + sumBases(r...); } };
struct A { int v = 1; }; struct B { int v = 2; }; struct C { int v = 3; };
template <class... Ts> int sizes() { int arr[] = {0, (int)sizeof(Ts)...}; int s = 0; for (int x : arr) s += x; return s; }
template <class... Ts> void printAll(const Ts &... ts) { int dummy[] = {0, (std::printf("%d ", (int)ts), 0)...}; (void)dummy; std::printf("\n"); }
template <class... Ts> int firstArity(int (*f)(Ts...)) { return sizeof...(Ts); }
int two(int, int) { return 0; }
int main() {
    std::printf("%d %d %d\n", count(), count(1, 2.0, 'c'), count("a", 1, 2, 3, 4));
    std::printf("%d %d\n", sum(1, 2, 3, 4), sum(1.5, 2));
    std::printf("%d\n", sumList(10, 20, 30));
    Tuple<int, double, char> t; t.head = 1; t.tail.head = 2.5; t.tail.tail.head = 'x';
    std::printf("%d %d %g %c\n", Tuple<int, double, char>::size, t.head, t.tail.head, t.tail.tail.head);
    Multi<A, B, C> m; std::printf("%d\n", m.all());
    std::printf("%d %d\n", sizes<char, int, double>(), sizes<>());
    printAll(1, 2.5, 'A');
    std::printf("%d\n", firstArity(two));
    return 0;
}
