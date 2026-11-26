#include <valarray>
#include <cstdio>
static void show(const char *n, const std::valarray<int> &v) {
    std::printf("%s:", n);
    for (std::size_t i = 0; i < v.size(); i++) std::printf(" %d", v[i]);
    std::printf("\n");
}
int main() {
    std::valarray<int> v = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    show("slice", v[std::slice(1, 3, 3)]);
    v[std::slice(0, 5, 2)] = 100;
    show("set", v);
    std::valarray<int> w(10);
    for (int i = 0; i < 10; i++) w[i] = i;
    std::valarray<bool> m = w > 6;
    show("mask", w[m]);
    std::size_t ixs[] = {9, 0, 4};
    std::valarray<std::size_t> ix(ixs, 3);
    show("ind", w[ix]);
    std::size_t lens[] = {2, 3}, strides[] = {5, 1};
    std::gslice g(0, std::valarray<std::size_t>(lens, 2), std::valarray<std::size_t>(strides, 2));
    show("gslice", w[g]);
    w[g] *= std::valarray<int>(2, 6);
    show("gmul", w);
    std::valarray<int> a = {1, 2, 3}, b = {4, 5, 6};
    show("add", a + b);
    show("mul", a * 2);
    show("shift", a.shift(1));
    show("cshift", a.cshift(-1));
    std::printf("%d %d %d\n", a.sum(), b.min(), b.max());
    std::valarray<double> d = {1.0, 4.0, 9.0};
    std::valarray<double> sq = std::sqrt(d);
    std::printf("%g %g %g\n", sq[0], sq[1], sq[2]);
    int tot = 0;
    for (int x : b) tot += x;
    std::printf("%d %d\n", tot, (int)(a == a).min());
    show("apply", a.apply([](int x) { return x * x; }));
    return 0;
}
