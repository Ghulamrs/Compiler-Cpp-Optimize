#include <cstdio>
#include <utility>
struct Has { void construct(int *p, int v) { *p = v + 100; } void destroy(int *) {} };
struct Lacks {};
template <class A> auto pick1(int, A &a) -> decltype(a.destroy((int *)0), 1) { return 1; }
template <class A> int pick1(long, A &) { return 2; }
template <class A> auto pick2(int, A &a) -> decltype(a.destroy((int *)0), void()) { std::printf("has2 "); }
template <class A> void pick2(long, A &) { std::printf("lacks2 "); }
template <class A, class... Args> auto pick3(int, A &a, int *p, Args &&... args) -> decltype(a.construct(p, std::forward<Args>(args)...), void()) { a.construct(p, std::forward<Args>(args)...); }
template <class A, class... Args> void pick3(long, A &, int *p, Args &&... args) { *p = -1; }
template <class A> auto pick4(int, A &a) -> decltype(a.destroy((int *)0)) { std::printf("has4 "); }
template <class A> void pick4(long, A &) { std::printf("lacks4 "); }
int main() {
    Has h; Lacks l;
    int x = 0, y = 0;
    std::printf("%d %d\n", pick1(0, h), pick1(0, l));
    pick2(0, h); pick2(0, l);
    pick4(0, h); pick4(0, l);
    pick3(0, h, &x, 5); pick3(0, l, &y, 5);
    std::printf("\n%d %d\n", x, y);
}
