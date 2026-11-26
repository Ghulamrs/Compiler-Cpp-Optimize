#include <cstdio>
#include <deque>
#include <queue>
#include <stack>
#include <string>
#include <vector>
#include <list>
#include <functional>
#include <stdexcept>
#include <algorithm>
static void show(const char *what, const std::deque<int> &d) {
    std::printf("%s:", what);
    for (std::deque<int>::const_iterator i = d.begin(); i != d.end(); ++i) std::printf(" %d", *i);
    std::printf(" (%d)\n", (int)d.size());
}
int main() {
    std::deque<int> d;
    for (int i = 0; i < 3000; i++) {
        if (i % 2) d.push_back(i);
        else d.push_front(i);
    }
    int *keep = &d[1500];
    for (int i = 0; i < 5000; i++) d.push_back(-i);
    std::printf("%d %d %d %d %d\n", (int)d.size(), d.front(), d[1499], *keep, d.back());
    std::deque<int> e = {1, 2, 3, 4, 5, 6, 7, 8};
    e.insert(e.begin() + 2, 99);
    e.insert(e.end() - 1, 2, 77);
    e.erase(e.begin() + 5);
    e.erase(e.begin(), e.begin() + 2);
    show("e", e);
    e.emplace(e.begin() + 1, 55);
    e.pop_front();
    e.pop_back();
    show("e2", e);
    std::sort(e.begin(), e.end());
    show("sorted", e);
    std::printf("%d %d %d\n", (int)(e.end() - e.begin()), *(e.rbegin() + 1), (int)std::count(e.begin(), e.end(), 77));
    try {
        e.at(100);
    } catch (const std::out_of_range &) {
        std::printf("out of range\n");
    }
    std::deque<std::string> s(2, "x");
    s.emplace_front("front");
    s.emplace_back(3, 'b');
    std::printf("%s %s %d\n", s.front().c_str(), s.back().c_str(), (int)(s == s));
    std::deque<int> f(e.begin(), e.end()), g;
    g = std::move(f);
    std::printf("%d %d %d\n", (int)f.size(), (int)g.size(), (int)(g < e || g == e));
    e.resize(3);
    e.resize(5, 9);
    show("resized", e);

    std::stack<int> st;
    for (int i = 1; i <= 4; i++) st.push(i * i);
    st.emplace(100);
    std::printf("stack %d %d", st.top(), (int)st.size());
    st.pop();
    std::printf(" %d\n", st.top());
    std::stack<int, std::vector<int> > sv;
    sv.push(3);
    std::queue<std::string> q;
    q.push("a");
    q.emplace("b");
    q.push(std::string("c"));
    std::printf("queue %s %s %d", q.front().c_str(), q.back().c_str(), (int)q.size());
    q.pop();
    std::printf(" %s\n", q.front().c_str());
    std::queue<int, std::list<int> > ql;
    ql.push(8);
    std::priority_queue<int> pq;
    int v[] = {5, 1, 9, 3, 7};
    for (int i = 0; i < 5; i++) pq.push(v[i]);
    std::printf("pq");
    while (!pq.empty()) {
        std::printf(" %d", pq.top());
        pq.pop();
    }
    std::printf("\n");
    std::priority_queue<int, std::vector<int>, std::greater<int> > mn(v, v + 5);
    std::printf("min %d %d %d %d\n", mn.top(), (int)mn.size(), sv.top(), ql.front());
    std::stack<int> s2 = st;
    std::printf("%d %d\n", (int)(s2 == st), (int)(s2 < st));
    return 0;
}
