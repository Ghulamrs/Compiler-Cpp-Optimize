#include <algorithm>
#include <cstdio>
#include <functional>
#include <set>
#include <string>
#include <utility>
#include <vector>
struct Odd { bool operator()(int x) const { return x % 2 != 0; } };
struct Below10 { bool operator()(int x) const { return x < 10; } };
struct Sum { int total; Sum() : total(0) {} void operator()(int x) { total += x; } };
bool same_parity(int a, int b) { return a % 2 == b % 2; }
bool near(int a, int b) { return a - b < 2 && b - a < 2; }
int main() {
    int a[] = {3, 1, 4, 1, 5, 9, 2, 6, 5, 3, 5};
    int *e = a + 11;
    std::printf("%d %d %d\n", (int)std::all_of(a, e, Odd()), (int)std::any_of(a, e, Odd()), (int)std::none_of(a, a, Odd()));
    Sum s = std::for_each(a, e, Sum());
    std::printf("sum %d\n", s.total);
    std::printf("%d %d %d\n", (int)(std::find_if(a, e, Odd()) - a), (int)(std::find_if_not(a, e, Odd()) - a), (int)(std::find(a, e, 7) - a));
    int pat[] = {5, 3};
    std::printf("%d %d\n", (int)(std::search(a, e, pat, pat + 2) - a), (int)(std::find_end(a, e, pat, pat + 1) - a));
    int one[] = {1};
    std::printf("%d %d\n", (int)(std::find_end(a, e, one, one + 1) - a), (int)(std::find_end(a, e, pat, pat) - a));
    int any[] = {9, 2};
    std::printf("%d %d\n", (int)(std::find_first_of(a, e, any, any + 2) - a), (int)(std::find_first_of(a, e, any, any + 2, near) - a));
    int b[] = {1, 2, 2, 7, 7, 7, 3};
    std::printf("%d %d\n", (int)(std::adjacent_find(b, b + 7) - b), (int)(std::adjacent_find(a, e, same_parity) - a));
    std::printf("%d %d\n", (int)(std::search_n(b, b + 7, 3, 7) - b), (int)(std::search_n(b, b + 7, 2, 2) - b));
    std::printf("%d %d %d\n", (int)(std::search_n(b, b + 7, 4, 7) - b), (int)(std::search_n(b, b + 7, 0, 9) - b),
        (int)(std::search_n(a, e, 2, 6, near) - a));
    std::printf("%d %d\n", (int)std::count(a, e, 5), (int)std::count_if(a, e, Odd()));
    int c[] = {3, 1, 4, 1, 6};
    std::pair<int *, int *> m = std::mismatch(a, a + 5, c);
    std::printf("mismatch %d %d %d\n", (int)(m.first - a), (int)(m.second - c), *m.first);
    m = std::mismatch(a, a + 5, c, same_parity);
    std::printf("mismatch %d\n", (int)(m.first - a));
    std::printf("%d %d\n", (int)std::equal(a, a + 4, c), (int)std::equal(a, a + 5, c, same_parity));
    int p[] = {5, 1, 3, 1, 4};
    int q[] = {5, 1, 3, 4, 4};
    std::printf("perm %d %d %d\n", (int)std::is_permutation(a, a + 5, p), (int)std::is_permutation(a, a + 5, q),
        (int)std::is_permutation(a, a + 5, q, same_parity));
    std::set<int> t(a, e);
    std::printf("set %d %d %d\n", (int)std::count_if(t.begin(), t.end(), Odd()), *std::find_if_not(t.begin(), t.end(), Odd()),
        (int)std::all_of(t.begin(), t.end(), Below10()));
    std::vector<std::string> w;
    w.push_back("pear");
    w.push_back("fig");
    w.push_back("plum");
    w.push_back("fig");
    std::string fig = "fig";
    std::printf("%d %d\n", (int)std::count(w.begin(), w.end(), fig), (int)(std::find_end(w.begin(), w.end(), &fig, &fig + 1) - w.begin()));
    return 0;
}
