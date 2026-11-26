#include <cstdio>
#include <vector>
#include <string>
struct Agg { int a, b; };
struct E { int v; E(int v) : v(v) { std::printf("E%d ", v); } ~E() { std::printf("~E%d ", v); } };
struct C { int x, y; C(int x, int y) : x(x), y(y) {} };
struct H {
    int arr[3];
    int zeroed[2];
    Agg agg;
    std::vector<int> vec;
    std::string str;
    C c;
    E es[2];
    int nested[2][2];
    H() : arr{1, 2, 3}, zeroed{}, agg{4, 5}, vec{6, 7, 8}, str{"nine"}, c{10, 11}, es{E(12), 13}, nested{{1, 2}, {3}} {}
};
int main() {
    {
        H h;
        std::printf("\n%d %d %d %d %d %d %d %d %d %d %s %d %d %d %d %d %d %d\n", h.arr[0], h.arr[1], h.arr[2], h.zeroed[0], h.zeroed[1],
                    h.agg.a, h.agg.b, (int)h.vec.size(), h.vec[0], h.vec[2], h.str.c_str(), h.c.x, h.c.y, h.es[0].v, h.es[1].v,
                    h.nested[0][1], h.nested[1][0], h.nested[1][1]);
    }
    std::printf("\n");
    return 0;
}
