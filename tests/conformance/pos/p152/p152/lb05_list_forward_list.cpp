#include <cstdio>
#include <list>
#include <forward_list>
#include <string>
#include <vector>
#include <iterator>
#include <algorithm>
static void show(const char *what, const std::list<int> &l) {
    std::printf("%s:", what);
    for (std::list<int>::const_iterator i = l.begin(); i != l.end(); ++i) std::printf(" %d", *i);
    std::printf(" (%d)\n", (int)l.size());
}
static void show(const char *what, const std::forward_list<int> &l) {
    std::printf("%s:", what);
    for (std::forward_list<int>::const_iterator i = l.begin(); i != l.end(); ++i) std::printf(" %d", *i);
    std::printf("\n");
}
struct Big {
    int k;
    std::string s;
    Big(int a, const char *b) : k(a), s(b) {}
};
int main() {
    std::list<int> a = {5, 3, 8, 1};
    a.push_front(9);
    a.push_back(2);
    show("a", a);
    a.sort();
    show("sorted", a);
    std::list<int> b(3, 4);
    a.merge(b);
    show("merged", a);
    a.unique();
    show("unique", a);
    a.remove(4);
    a.reverse();
    show("reversed", a);
    std::list<int>::iterator i = a.begin();
    std::advance(i, 2);
    a.insert(i, 2, 7);
    i = a.erase(a.begin());
    show("insert/erase", a);
    std::list<int> c = {100, 200};
    a.splice(a.begin(), c);
    a.splice(a.end(), a, a.begin());
    show("splice", a);
    std::printf("%d %d %d\n", (int)c.empty(), a.front(), a.back());
    a.remove_if([](int x) { return x > 50; });
    show("remove_if", a);
    std::list<Big> g;
    g.emplace_back(1, "one");
    g.emplace_front(0, "zero");
    g.emplace(++g.begin(), 5, "five");
    for (std::list<Big>::iterator j = g.begin(); j != g.end(); ++j) std::printf("%d%s ", j->k, j->s.c_str());
    std::printf("\n");
    std::list<int> d(a.rbegin(), a.rend());
    show("from reverse", d);
    std::printf("%d %d %d\n", (int)(a == a), (int)(a < d), (int)(d != a));
    a.resize(2);
    a.resize(4, -1);
    show("resized", a);
    std::vector<int> v(a.begin(), a.end());
    std::printf("%d\n", (int)v.size());

    std::forward_list<int> f = {4, 1, 3};
    f.push_front(2);
    show("f", f);
    f.sort();
    show("f sorted", f);
    f.insert_after(f.before_begin(), 0);
    std::forward_list<int>::iterator k = f.begin();
    ++k;
    f.insert_after(k, 3, 9);
    show("f inserted", f);
    f.unique();
    f.remove(9);
    show("f unique/remove", f);
    std::forward_list<int> h = {5, 6};
    f.splice_after(f.before_begin(), h);
    show("f splice", f);
    f.reverse();
    show("f reversed", f);
    std::forward_list<int> m = {1, 5, 10};
    std::forward_list<int> n = {2, 6};
    m.merge(n);
    show("m merged", m);
    m.erase_after(m.begin());
    m.resize(6, 8);
    show("m resized", m);
    std::printf("%d %d %d\n", (int)(m == m), (int)n.empty(), (int)std::distance(m.begin(), m.end()));
    return 0;
}
