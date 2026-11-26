// [dcl.init.list]/6: the array behind an initializer_list lives as long as the list - one with static storage,
// at namespace scope or as a static local, keeps its elements for the whole program.
#include <cstdio>
#include <initializer_list>

struct Point { int x, y; };

static std::initializer_list<int> primes = {2, 3, 5, 7};
std::initializer_list<double> halves = {0.5, 1.5};
std::initializer_list<Point> corners = {{0, 0}, {3, 4}};

static int total(std::initializer_list<int> l) {
    int s = 0;
    for (int v : l) s += v;
    return s;
}

static const std::initializer_list<int> &cached() {
    static std::initializer_list<int> once = {10, 20, 30};
    return once;
}

int main() {
    std::printf("%d %d", static_cast<int>(primes.size()), total(primes));
    for (double h : halves) std::printf(" %.1f", h);
    for (const Point &p : corners) std::printf(" (%d,%d)", p.x, p.y);
    std::printf(" %d %d\n", total(cached()), *(cached().begin() + 1));
    return 0;
}
