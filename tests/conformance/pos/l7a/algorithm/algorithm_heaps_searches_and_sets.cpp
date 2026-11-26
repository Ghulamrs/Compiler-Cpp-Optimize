#include <algorithm>
#include <cstdio>
#include <functional>
#include <set>
#include <utility>
#include <vector>
struct ByFirst { bool operator()(const std::pair<int, char> &a, const std::pair<int, char> &b) const { return a.first < b.first; } };
void show(const char *what, const int *from, const int *to) {
    std::printf("%s:", what);
    for (; from != to; ++from) std::printf(" %d", *from);
    std::printf("\n");
}
int main() {
    int h[] = {3, 1, 4, 1, 5, 9, 2, 6, 5, 3, 5, 8, 9, 7};
    std::make_heap(h, h + 14);
    show("make_heap", h, h + 14);
    std::printf("%d %d\n", (int)std::is_heap(h, h + 14), (int)(std::is_heap_until(h, h + 14) - h));
    std::pop_heap(h, h + 14);
    show("pop_heap", h, h + 14);
    h[13] = 10;
    std::push_heap(h, h + 14);
    show("push_heap", h, h + 14);
    std::sort_heap(h, h + 14);
    show("sort_heap", h, h + 14);
    std::printf("%d %d\n", (int)std::is_heap(h, h + 14), (int)(std::is_heap_until(h, h + 14) - h));
    int g[] = {5, 2, 8, 2, 7, 1};
    std::make_heap(g, g + 6, std::greater<int>());
    show("make_heap>", g, g + 6);
    std::pop_heap(g, g + 6, std::greater<int>());
    std::printf("%d %d\n", g[5], (int)std::is_heap(g, g + 5, std::greater<int>()));
    std::sort_heap(g, g + 5, std::greater<int>());
    show("sort_heap>", g, g + 5);
    int s[] = {1, 2, 2, 2, 4, 6, 6, 9};
    std::printf("%d %d %d %d\n", (int)(std::lower_bound(s, s + 8, 2) - s), (int)(std::upper_bound(s, s + 8, 2) - s),
        (int)(std::lower_bound(s, s + 8, 5) - s), (int)(std::upper_bound(s, s + 8, 10) - s));
    std::pair<int *, int *> r = std::equal_range(s, s + 8, 6);
    std::printf("%d %d %d %d %d\n", (int)(r.first - s), (int)(r.second - s), (int)std::binary_search(s, s + 8, 4),
        (int)std::binary_search(s, s + 8, 5), (int)std::binary_search(s, s + 8, 0));
    int down[] = {9, 6, 6, 4, 2, 1};
    std::printf("%d %d\n", (int)(std::lower_bound(down, down + 6, 6, std::greater<int>()) - down),
        (int)(std::upper_bound(down, down + 6, 6, std::greater<int>()) - down));
    std::set<int> t(s, s + 8);
    std::set<int>::const_iterator lo = std::lower_bound(t.begin(), t.end(), 3);
    std::set<int>::const_iterator hi = std::upper_bound(t.begin(), t.end(), 6);
    std::printf("set %d %d %d\n", *lo, hi == t.end() ? -1 : *hi, (int)std::binary_search(t.begin(), t.end(), 9));
    int a[] = {1, 3, 5, 7};
    int b[] = {2, 3, 4, 8, 9};
    int out[16];
    int *end = std::merge(a, a + 4, b, b + 5, out);
    show("merge", out, end);
    std::vector<std::pair<int, char> > p;
    p.push_back(std::make_pair(1, 'a'));
    p.push_back(std::make_pair(3, 'a'));
    p.push_back(std::make_pair(5, 'a'));
    p.push_back(std::make_pair(1, 'b'));
    p.push_back(std::make_pair(3, 'b'));
    p.push_back(std::make_pair(4, 'b'));
    std::inplace_merge(p.begin(), p.begin() + 3, p.end(), ByFirst());
    for (int i = 0; i < 6; i++) std::printf("%d%c ", p[i].first, p[i].second);
    std::printf("\n");
    int m[] = {2, 4, 6, 8, 10, 1, 3, 5, 7, 9, 11, 12};
    std::inplace_merge(m, m + 5, m + 12);
    show("inplace_merge", m, m + 12);
    int x[] = {1, 2, 2, 3, 5, 8};
    int y[] = {2, 3, 3, 4, 8};
    int sub[] = {2, 5};
    std::printf("includes %d %d\n", (int)std::includes(x, x + 6, sub, sub + 2), (int)std::includes(x, x + 6, y, y + 5));
    end = std::set_union(x, x + 6, y, y + 5, out);
    show("set_union", out, end);
    end = std::set_intersection(x, x + 6, y, y + 5, out);
    show("set_intersection", out, end);
    end = std::set_difference(x, x + 6, y, y + 5, out);
    show("set_difference", out, end);
    end = std::set_symmetric_difference(x, x + 6, y, y + 5, out);
    show("set_symmetric_difference", out, end);
    int xd[] = {8, 5, 3, 2};
    int yd[] = {9, 5, 2, 1};
    end = std::set_union(xd, xd + 4, yd, yd + 4, out, std::greater<int>());
    show("set_union>", out, end);
    return 0;
}
