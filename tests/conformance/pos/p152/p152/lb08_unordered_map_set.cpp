#include <cstdio>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <vector>
#include <algorithm>
#include <stdexcept>
// Iteration order is each library's own, so what is printed is sorted first.
template <class C> static std::vector<typename C::value_type> sorted(const C &c) {
    std::vector<typename C::value_type> v(c.begin(), c.end());
    std::sort(v.begin(), v.end());
    return v;
}
int main() {
    std::unordered_map<std::string, int> m;
    m["one"] = 1;
    m["two"] = 2;
    m.insert(std::make_pair(std::string("three"), 3));
    std::pair<std::unordered_map<std::string, int>::iterator, bool> r = m.emplace("one", 100);
    std::printf("%d %d %d %d\n", (int)m.size(), (int)r.second, r.first->second, m.at("two"));
    try {
        m.at("four");
    } catch (const std::out_of_range &) {
        std::printf("no four\n");
    }
    for (int i = 0; i < 1000; i++) m[std::to_string(i)] = i;
    std::printf("%d %d %d %d\n", (int)m.size(), m["500"], (int)m.count("999"), (int)m.count("1000"));
    std::printf("%d\n", (int)(m.load_factor() <= m.max_load_factor()));
    m.erase("500");
    std::unordered_map<std::string, int>::iterator f = m.find("one");
    m.erase(f);
    std::printf("%d %d %d\n", (int)m.size(), (int)(m.find("500") == m.end()), (int)m.count("one"));
    std::unordered_map<std::string, int> n = m, o;
    std::printf("%d", (int)(n == m));
    n["x"] = 1;
    std::printf(" %d", (int)(n != m));
    o = std::move(n);
    std::printf(" %d %d\n", (int)o.size(), (int)n.size());

    std::unordered_multimap<int, char> mm = {{1, 'a'}, {2, 'b'}, {1, 'c'}, {3, 'd'}, {1, 'e'}};
    std::pair<std::unordered_multimap<int, char>::iterator, std::unordered_multimap<int, char>::iterator> eq = mm.equal_range(1);
    std::string got;
    for (; eq.first != eq.second; ++eq.first) got += eq.first->second;
    std::sort(got.begin(), got.end());
    std::printf("%d %d %s %d\n", (int)mm.size(), (int)mm.count(1), got.c_str(), (int)mm.erase(1));
    std::printf("%d\n", (int)mm.size());

    std::unordered_set<int> s = {5, 3, 9, 3, 1};
    s.insert(7);
    s.emplace(5);
    std::vector<int> sv = sorted(s);
    for (std::size_t i = 0; i < sv.size(); i++) std::printf("%d ", sv[i]);
    std::printf("| %d %d\n", (int)s.count(9), (int)s.count(4));
    std::unordered_multiset<std::string> ms;
    ms.insert("a");
    ms.insert("b");
    ms.insert("a");
    const int msSize = (int)ms.size(), msCount = (int)ms.count("a");
    std::printf("%d %d %d\n", msSize, msCount, (int)ms.erase("a"));
    std::unordered_set<int> t(sv.begin(), sv.end());
    std::printf("%d %d\n", (int)(t == s), (int)(t.bucket_count() > 0));
    std::size_t total = 0;
    for (std::size_t b = 0; b < s.bucket_count(); b++) total += s.bucket_size(b);
    std::printf("%d\n", (int)total);
    s.rehash(100);
    s.reserve(1000);
    std::printf("%d %d\n", (int)(s.bucket_count() >= 100), (int)s.size());
    return 0;
}
