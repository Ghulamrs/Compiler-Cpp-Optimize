#include <algorithm>
#include <cstdio>
#include <functional>
#include <initializer_list>
#include <string>
#include <utility>
struct Key {
    int k;
    char tag;
    Key() : k(0), tag(0) {}
    Key(int k, char tag) : k(k), tag(tag) {}
};
bool operator<(const Key &a, const Key &b) { return a.k < b.k; }
bool by_last(const std::string &a, const std::string &b) { return a[a.size() - 1] < b[b.size() - 1]; }
int main() {
    Key p(1, 'p'), q(1, 'q'), r(2, 'r');
    std::printf("%c %c %c %c\n", std::min(p, q).tag, std::max(p, q).tag, std::min(r, p).tag, std::max(p, r).tag);
    std::printf("%d %d\n", std::min(3, 7, std::greater<int>()), std::max(3, 7, std::greater<int>()));
    std::pair<const Key &, const Key &> mm = std::minmax(p, q);
    std::printf("minmax %c %c\n", mm.first.tag, mm.second.tag);
    std::pair<const int &, const int &> m2 = std::minmax(9, 4);
    std::printf("minmax %d %d\n", m2.first, m2.second);
    std::pair<const int &, const int &> m3 = std::minmax(9, 4, std::greater<int>());
    std::printf("minmax> %d %d\n", m3.first, m3.second);
    std::printf("%d %d\n", std::min({4, 2, 8, 2}), std::max({4, 9, 8, 9}));
    std::printf("%c %c\n", std::min({p, q, r}).tag, std::max({r, p, Key(2, 's')}).tag);
    std::printf("%d %d\n", std::min({4, 2, 8}, std::greater<int>()), std::max({4, 2, 8}, std::greater<int>()));
    std::pair<int, int> m4 = std::minmax({5, 1, 9, 1, 9, 3});
    std::printf("minmax(il) %d %d\n", m4.first, m4.second);
    std::pair<Key, Key> m5 = std::minmax({q, r, p, Key(2, 't')});
    std::printf("minmax(il) %c %c\n", m5.first.tag, m5.second.tag);
    int a[] = {5, 1, 9, 1, 9, 3, 9};
    for (int n = 0; n <= 7; n++) {
        std::pair<int *, int *> e = std::minmax_element(a, a + n);
        std::printf("[%d %d]", (int)(e.first - a), (int)(e.second - a));
    }
    std::printf("\n");
    std::pair<int *, int *> e = std::minmax_element(a, a + 7, std::greater<int>());
    std::printf("%d %d\n", (int)(e.first - a), (int)(e.second - a));
    std::string w1 = "apple", w2 = "apply", w3 = "app";
    std::printf("%d %d %d %d\n", (int)std::lexicographical_compare(w1.begin(), w1.end(), w2.begin(), w2.end()),
        (int)std::lexicographical_compare(w2.begin(), w2.end(), w1.begin(), w1.end()),
        (int)std::lexicographical_compare(w3.begin(), w3.end(), w1.begin(), w1.end()),
        (int)std::lexicographical_compare(w1.begin(), w1.end(), w1.begin(), w1.end()));
    std::printf("%d\n", (int)std::lexicographical_compare(w1.begin(), w1.end(), w2.begin(), w2.end(), std::greater<char>()));
    std::string s = "abc";
    do std::printf("%s ", s.c_str()); while (std::next_permutation(s.begin(), s.end()));
    std::printf("| %s\n", s.c_str());
    s = "cba";
    do std::printf("%s ", s.c_str()); while (std::prev_permutation(s.begin(), s.end()));
    std::printf("| %s\n", s.c_str());
    int d[] = {1, 1, 2, 2};
    int count = 0;
    do count++; while (std::next_permutation(d, d + 4));
    std::printf("%d %d%d%d%d\n", count, d[0], d[1], d[2], d[3]);
    int g[] = {3, 2, 1};
    count = 0;
    do count++; while (std::next_permutation(g, g + 3, std::greater<int>()));
    std::printf("%d %d%d%d\n", count, g[0], g[1], g[2]);
    std::string words[] = {"kiwi", "lime", "fig"};
    std::printf("%s %s\n", std::min_element(words, words + 3, by_last)->c_str(), std::max(words[0], words[2], by_last).c_str());
    return 0;
}
