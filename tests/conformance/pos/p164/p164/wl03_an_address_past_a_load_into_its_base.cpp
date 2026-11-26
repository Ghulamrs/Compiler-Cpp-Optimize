// `n` is copied to where its member's address is made, and that register is loaded with something else before the
// member is read: the load has to go through the address made, not through the register it was made from.
#include <algorithm>
#include <cstdio>
#include <forward_list>
#include <map>
#include <set>
struct Link { Link *left; Link *right; int colour; int pad; int key; };
struct Keyed {
    bool operator()(const int &a, const int &b) const { return a < b; }
};
struct Tree {
    Link head;
    Keyed less;
    Link *lower(const int &) const { return head.left; }
    static int &keyOf(Link *n) { return n->key; }
    Link *find(const int &k) const {
        Link *at = lower(k);
        if (at == &head || less(k, keyOf(at))) return const_cast<Link *>(&head);
        return at;
    }
};
int main() {
    Link n = { 0, 0, 0, 0, 7 };
    Tree t;
    t.head.left = &n;
    std::printf("%d %d\n", t.find(7) == &n ? 1 : 0, t.find(6) == &n ? 1 : 0);
    std::map<int, int> m;
    m[1] = 10;
    m[3] = 30;
    std::printf("%d %d\n", m.find(3) == m.end() ? 0 : m.find(3)->second, m.find(2) == m.end() ? 1 : 0);
    std::set<int> s;
    s.insert(4);
    s.insert(2);
    std::printf("%d %d\n", std::binary_search(s.begin(), s.end(), 2) ? 1 : 0, std::binary_search(s.begin(), s.end(), 3) ? 1 : 0);
    std::forward_list<int> x, y;
    x.push_front(5);
    x.push_front(1);
    y.push_front(4);
    y.push_front(2);
    x.merge(y);
    for (std::forward_list<int>::iterator i = x.begin(); i != x.end(); ++i) std::printf("%d ", *i);
    std::printf("\n");
    return 0;
}
