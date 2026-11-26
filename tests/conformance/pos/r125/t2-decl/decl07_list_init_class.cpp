// [over.match.list]: initializer_list constructor wins over other constructors; empty braces pick the default constructor; nested braces; brace elision in aggregates of aggregates.
#include <cstdio>
#include <initializer_list>
#include <vector>
struct IL {
    IL(std::initializer_list<int> l) : n((int)l.size()), kind('L') {}
    IL(int a, int b) : n(a + b), kind('C') {}
    IL() : n(-1), kind('D') {}
    int n; char kind;
};
struct Agg { int a[2]; int b; };
struct Outer { Agg in; int c; };
struct Pt { int x, y; };
struct HasPt { Pt p; Pt q; };
int main() {
    IL a{1, 2}; IL b(1, 2); IL c{}; IL d{1, 2, 3, 4}; IL e = {5};
    std::printf("%d%c %d%c %d%c %d%c %d%c\n", a.n, a.kind, b.n, b.kind, c.n, c.kind, d.n, d.kind, e.n, e.kind);
    Agg g = {1, 2, 3}; Agg g2 = {{1, 2}, 3};
    std::printf("%d %d %d %d\n", g.a[1], g.b, g2.a[0], g2.b);
    Outer o = {1, 2, 3, 4}; Outer o2 = {{{5, 6}, 7}, 8};
    std::printf("%d %d %d %d %d %d\n", o.in.a[0], o.in.a[1], o.in.b, o.c, o2.in.b, o2.c);
    HasPt h = {{1, 2}, {3, 4}}; HasPt h2 = {1, 2, 3};
    std::printf("%d %d %d %d\n", h.q.x, h.q.y, h2.q.x, h2.q.y);
    std::vector<int> v{1, 2, 3}; std::vector<int> v2(3, 9); std::vector<int> v3{3, 9};
    std::printf("%d %d %d %d\n", (int)v.size(), (int)v2.size(), (int)v3.size(), v3[1]);
    std::vector<Pt> vp = {{1, 2}, {3, 4}};
    std::printf("%d %d\n", (int)vp.size(), vp[1].y);
    int arr[2][3] = {1, 2, 3, 4, 5, 6}; int arr2[2][3] = {{1}, {4}};
    std::printf("%d %d %d %d\n", arr[1][0], arr[1][2], arr2[1][0], arr2[0][1]);
    return 0;
}
