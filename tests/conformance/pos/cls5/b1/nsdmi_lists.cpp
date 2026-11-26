#include <cstdio>
#include <vector>
#include <string>
struct Agg { int a, b; };
struct C { int x, y; C() : x(-1), y(-1) {} C(int x, int y) : x(x), y(y) {} };
struct Z { int q; Z() = default; };
struct E { int v; E(int v) : v(v) { std::printf("E%d ", v); } ~E() { std::printf("~E%d ", v); } };
struct S {
    int arr[3] = {1, 2, 3};
    int part[4]{5, 6};
    Agg agg{7, 8};
    Agg agg2 = {9};
    std::vector<int> vec{10, 11};
    std::string str = {"twelve"};
    C c{13, 14};
    C d{};
    Z z{};
    E es[2] = {15, 16};
};
struct T { S s; int extra = 17; T() {} };
int main() {
    {
        S s;
        T t;
        std::printf("\n%d %d %d %d %d %d %d %d %d %d %d %s %d %d %d %d %d %d\n", s.arr[0], s.arr[2], s.part[1], s.part[3], s.agg.a, s.agg.b,
                    s.agg2.a, s.agg2.b, (int)s.vec.size(), s.vec[1], s.c.x, s.str.c_str(), s.c.y, s.d.x, s.z.q, s.es[1].v,
                    t.s.arr[1], t.extra);
    }
    std::printf("\n");
    return 0;
}
