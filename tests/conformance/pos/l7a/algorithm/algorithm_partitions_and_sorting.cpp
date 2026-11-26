#include <algorithm>
#include <cstdio>
#include <functional>
#include <utility>
#include <vector>
struct Odd { bool operator()(int x) const { return x % 2 != 0; } };
struct Below5 { bool operator()(int x) const { return x < 5; } };
struct ByFirst { bool operator()(const std::pair<int, char> &a, const std::pair<int, char> &b) const { return a.first < b.first; } };
void show(const char *what, const int *from, const int *to) {
    std::printf("%s:", what);
    for (; from != to; ++from) std::printf(" %d", *from);
    std::printf("\n");
}
int main() {
    int a[] = {1, 3, 5, 2, 4, 6};
    std::printf("%d %d\n", (int)std::is_partitioned(a, a + 6, Odd()), (int)std::is_partitioned(a, a + 6, Below5()));
    std::printf("%d\n", (int)(std::partition_point(a, a + 6, Odd()) - a));
    int b[] = {2, 7, 4, 9, 6, 1, 8, 3, 5, 0};
    int *cut = std::partition(b, b + 10, Odd());
    std::printf("partition %d %d %d\n", (int)(cut - b), (int)std::is_partitioned(b, b + 10, Odd()), (int)std::count_if(b, cut, Odd()));
    int c[] = {2, 7, 4, 9, 6, 1, 8, 3, 5, 0};
    cut = std::stable_partition(c, c + 10, Odd());
    std::printf("%d ", (int)(cut - c));
    show("stable_partition", c, c + 10);
    cut = std::stable_partition(c, c + 10, Below5());
    std::printf("%d ", (int)(cut - c));
    show("stable_partition", c, c + 10);
    int yes[10], no[10];
    std::pair<int *, int *> ends = std::partition_copy(b, b + 10, yes, no, Below5());
    std::sort(yes, ends.first);
    std::sort(no, ends.second);
    show("partition_copy yes", yes, ends.first);
    show("partition_copy no", no, ends.second);
    int d[] = {9, 4, 7, 1, 8, 2, 6, 3, 5, 0, 11, 10};
    std::partial_sort(d, d + 4, d + 12);
    show("partial_sort", d, d + 4);
    std::partial_sort(d, d + 3, d + 12, std::greater<int>());
    show("partial_sort>", d, d + 3);
    int e[] = {5, 3, 8};
    int f[5];
    int *end = std::partial_sort_copy(d, d + 12, f, f + 5);
    show("partial_sort_copy", f, end);
    end = std::partial_sort_copy(e, e + 3, f, f + 5, std::greater<int>());
    show("partial_sort_copy>", f, end);
    int g[] = {1, 2, 2, 5, 3, 4};
    std::printf("%d %d %d %d\n", (int)std::is_sorted(g, g + 4), (int)std::is_sorted(g, g + 6), (int)(std::is_sorted_until(g, g + 6) - g),
        (int)(std::is_sorted_until(g, g + 6, std::greater<int>()) - g));
    std::vector<int> h;
    for (int i = 0; i < 101; i++) h.push_back((i * 37) % 101);
    for (int k = 0; k < 101; k += 25) {
        std::vector<int> w = h;
        std::nth_element(w.begin(), w.begin() + k, w.end());
        bool ok = true;
        for (int i = 0; i < k; i++) if (w[i] > w[k]) ok = false;
        for (int i = k + 1; i < 101; i++) if (w[i] < w[k]) ok = false;
        std::printf("nth %d: %d %d\n", k, w[k], (int)ok);
    }
    int n[] = {4, 1, 3};
    std::nth_element(n, n + 1, n + 3, std::greater<int>());
    std::printf("nth> %d\n", n[1]);
    std::vector<std::pair<int, char> > p;
    const char *letters = "dcbaedcbaedcba";
    for (int i = 0; i < 14; i++) p.push_back(std::make_pair(i % 3, letters[i]));
    std::stable_sort(p.begin(), p.end(), ByFirst());
    for (int i = 0; i < 14; i++) std::printf("%d%c ", p[i].first, p[i].second);
    std::printf("\n");
    std::sort(h.begin(), h.end(), std::greater<int>());
    std::printf("sort> %d %d %d %d\n", h[0], h[1], h[99], h[100]);
    return 0;
}
