#include <cstdio>
#include <functional>
#include <initializer_list>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
struct ByLength {
    bool operator()(const std::string &a, const std::string &b) const { return a.size() < b.size(); }
};
int main() {
    std::map<int, char> m{{3, 'c'}, {1, 'a'}, {2, 'b'}};
    std::printf("%d %c %c\n", (int)m.size(), m.at(2), m.cbegin()->second);
    try {
        m.at(9);
    } catch (const std::out_of_range &e) {
        std::printf("at: %s\n", e.what());
    }
    std::printf("reverse:");
    for (std::map<int, char>::reverse_iterator i = m.rbegin(); i != m.rend(); ++i) std::printf(" %d%c", i->first, i->second);
    std::printf(" |");
    for (std::map<int, char>::const_reverse_iterator i = m.crbegin(); i != m.crend(); ++i) std::printf(" %d", i->first);
    std::printf("\n");
    std::pair<std::map<int, char>::iterator, std::map<int, char>::iterator> r = m.equal_range(2);
    std::printf("%d %d\n", r.first->first, r.second->first);
    std::initializer_list<std::pair<const int, char> > more = {{5, 'e'}, {4, 'd'}, {1, 'z'}};
    m.insert(more);
    std::printf("%d %c\n", (int)m.size(), m[1]);
    std::map<int, char>::iterator after = m.erase(m.find(2), m.find(5));
    std::printf("%d %d\n", after->first, (int)m.size());
    std::printf("%d %d\n", (int)m.key_comp()(1, 2), (int)m.value_comp()(*m.begin(), *m.rbegin()));
    std::map<int, char, std::greater<int> > down{{1, 'x'}, {3, 'y'}, {2, 'z'}};
    std::printf("greater:");
    for (std::map<int, char, std::greater<int> >::const_iterator i = down.begin(); i != down.end(); ++i) std::printf(" %d", i->first);
    std::printf(" %d\n", (int)down.lower_bound(2)->first);
    std::map<std::string, int, ByLength> lengths;
    lengths["abc"] = 1;
    lengths["xyz"] = 2;
    lengths["q"] = 3;
    std::printf("%d %d %d\n", (int)lengths.size(), lengths["abc"], lengths.begin()->second);
    std::multimap<int, std::string> mm{{2, "b"}, {1, "a"}, {2, "bb"}, {3, "c"}, {2, "bbb"}};
    mm.insert(std::make_pair(2, std::string("bbbb")));
    std::printf("multimap %d %d:", (int)mm.size(), (int)mm.count(2));
    std::pair<std::multimap<int, std::string>::iterator, std::multimap<int, std::string>::iterator> two = mm.equal_range(2);
    for (; two.first != two.second; ++two.first) std::printf(" %s", two.first->second.c_str());
    std::printf(" | erased %d left %d\n", (int)mm.erase(2), (int)mm.size());
    std::multimap<int, std::string> mm2 = mm;
    std::printf("%d %d\n", (int)(mm2 == mm), (int)(mm2.begin()->second == "a"));
    std::set<int> s{5, 1, 4, 1, 3};
    std::printf("set %d:", (int)s.size());
    for (std::set<int>::const_reverse_iterator i = s.rbegin(); i != s.rend(); ++i) std::printf(" %d", *i);
    std::pair<std::set<int>::const_iterator, std::set<int>::const_iterator> e = s.equal_range(4);
    std::printf(" | %d %d %d\n", *e.first, *e.second, (int)s.key_comp()(1, 2));
    s.insert({9, 0});
    s.erase(s.find(4), s.end());
    std::printf("set:");
    for (std::set<int>::const_iterator i = s.cbegin(); i != s.cend(); ++i) std::printf(" %d", *i);
    std::printf("\n");
    std::set<int, std::greater<int> > sd{1, 3, 2};
    std::printf("%d %d\n", *sd.begin(), (int)sd.count(3));
    std::multiset<int> ms{2, 1, 2, 3, 2};
    std::printf("multiset %d %d", (int)ms.size(), (int)ms.count(2));
    ms.erase(ms.find(2));
    std::printf(" %d", (int)ms.count(2));
    ms.insert(0);
    std::printf(" %d %d\n", *ms.begin(), (int)ms.erase(2));
    std::set<int> a{1, 2}, b{1, 3};
    std::printf("%d %d %d %d\n", (int)(a < b), (int)(a > b), (int)(a <= a), (int)(b >= a));
    swap(a, b);
    std::printf("%d\n", *a.rbegin());
    std::map<int, int> moved{{1, 1}};
    std::map<int, int> target(std::move(moved));
    std::printf("%d %d\n", (int)target.size(), (int)moved.size());
    return 0;
}
