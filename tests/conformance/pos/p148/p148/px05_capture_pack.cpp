// [expr.prim.lambda]/23: a capture followed by '...' captures every member of a function parameter pack.
#include <cstdio>
template <class... Ts> int byCopy(Ts... ts) {
    auto l = [ts...] { int a[] = {0, (int)ts...}; int s = 0; for (int x : a) s += x; return s; };
    return l();
}
template <class... Ts> void byRef(Ts &... ts) { auto l = [&ts...] { int a[] = {0, (ts += 1, 0)...}; (void)a; }; l(); }
template <class... Ts> int mixed(int k, Ts... ts) { auto l = [k, ts...]() { return k * (int)sizeof...(ts); }; return l(); }
int main() {
    int a = 1, b = 2;
    byRef(a, b);
    std::printf("%d %d %d %d %d\n", byCopy(1, 2, 3), byCopy(), a, b, mixed(5, 'x', 2.0));
    return 0;
}
