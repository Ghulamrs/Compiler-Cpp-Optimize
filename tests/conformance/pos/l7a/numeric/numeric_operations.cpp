#include <cstdio>
#include <functional>
#include <numeric>
#include <set>
#include <string>
#include <vector>
struct Max { int operator()(int a, int b) const { return a < b ? b : a; } };
struct Same { int operator()(int a, int b) const { return a == b ? 1 : 0; } };
void show(const char *what, const int *from, const int *to) {
    std::printf("%s:", what);
    for (; from != to; ++from) std::printf(" %d", *from);
    std::printf("\n");
}
int main() {
    int a[] = {1, 2, 3, 4, 5};
    int b[] = {6, 5, 4, 3, 2};
    std::printf("%d %d\n", std::accumulate(a, a + 5, 0), std::accumulate(a, a + 5, 1, std::multiplies<int>()));
    std::printf("%d %d\n", std::inner_product(a, a + 5, b, 0), std::inner_product(a, a + 5, b, 0, std::plus<int>(), Same()));
    std::printf("%g\n", std::inner_product(a, a + 3, b, 0.5));
    int out[8];
    int *end = std::partial_sum(a, a + 5, out);
    show("partial_sum", out, end);
    end = std::partial_sum(b, b + 5, out, Max());
    show("partial_sum(max)", out, end);
    end = std::partial_sum(a, a + 5, a);
    show("partial_sum in place", a, end);
    int c[] = {2, 3, 5, 8, 13, 21};
    end = std::adjacent_difference(c, c + 6, out);
    show("adjacent_difference", out, end);
    end = std::adjacent_difference(c, c + 6, out, std::plus<int>());
    show("adjacent_difference(+)", out, end);
    end = std::adjacent_difference(c, c + 6, c);
    show("adjacent_difference in place", c, end);
    std::printf("%d\n", (int)(std::partial_sum(a, a, out) - out));
    std::vector<int> v(6);
    std::iota(v.begin(), v.end(), -2);
    show("iota", &v[0], &v[0] + 6);
    std::vector<double> d(3);
    std::iota(d.begin(), d.end(), 0.25);
    std::printf("%g %g %g\n", d[0], d[1], d[2]);
    std::set<int> s(b, b + 5);
    std::printf("%d\n", std::accumulate(s.begin(), s.end(), 100));
    std::vector<std::string> w;
    w.push_back("ab");
    w.push_back("cd");
    w.push_back("e");
    std::printf("%s\n", std::accumulate(w.begin(), w.end(), std::string("<")).c_str());
    std::vector<std::string> sums(3);
    std::partial_sum(w.begin(), w.end(), sums.begin());
    std::printf("%s %s %s\n", sums[0].c_str(), sums[1].c_str(), sums[2].c_str());
    return 0;
}
