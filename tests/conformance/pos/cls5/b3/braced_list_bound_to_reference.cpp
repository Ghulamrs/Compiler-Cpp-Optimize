#include <cstdio>
#include <initializer_list>
int main() {
    auto l = {1, 2, 3};
    const auto &r = {5, 6};
    auto &&q = {7, 8, 9, 10};
    const std::initializer_list<int> &e = {11};
    int s = 0;
    for (int x : r) s += x;
    for (int x : q) s += x;
    std::printf("%d %d %d %d %d\n", (int)l.size(), (int)r.size(), (int)q.size(), (int)e.size(), s + *e.begin());
    return 0;
}
