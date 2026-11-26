#include <cstdio>
#include <list>
#include <forward_list>
#include <deque>
#include <map>
#include <set>
#include <unordered_map>
#include <memory>
#include <string>
#include <utility>
static int live = 0, made = 0, gone = 0;
template <class T> struct Count {
    typedef T value_type;
    Count() {}
    template <class U> Count(const Count<U> &) {}
    T *allocate(std::size_t n) { ++live; return static_cast<T *>(::operator new(n * sizeof(T))); }
    void deallocate(T *p, std::size_t) { --live; ::operator delete(p); }
    template <class U, class... A> void construct(U *p, A &&... a) { ++made; ::new ((void *)p) U(std::forward<A>(a)...); }
    template <class U> void destroy(U *p) { ++gone; p->~U(); }
};
template <class T, class U> bool operator==(const Count<T> &, const Count<U> &) { return true; }
template <class T, class U> bool operator!=(const Count<T> &, const Count<U> &) { return false; }
struct Heavy {
    static int copies;
    int v;
    explicit Heavy(int x) : v(x) {}
    Heavy(const Heavy &o) : v(o.v) { ++copies; }
    bool operator<(const Heavy &o) const { return v < o.v; }
};
int Heavy::copies = 0;
int main() {
    {
        std::list<int, Count<int> > l;
        std::forward_list<int, Count<int> > f;
        std::deque<int, Count<int> > d;
        for (int i = 0; i < 300; i++) { l.push_back(i); f.push_front(i); d.push_back(i); d.push_front(-i); }
        std::map<int, std::string, std::less<int>, Count<std::pair<const int, std::string> > > m;
        std::set<int, std::less<int>, Count<int> > s;
        std::multimap<int, int, std::less<int>, Count<std::pair<const int, int> > > mm;
        std::unordered_map<int, int, std::hash<int>, std::equal_to<int>, Count<std::pair<const int, int> > > um;
        for (int i = 0; i < 50; i++) { m[i] = "x"; s.insert(i); mm.emplace(i % 5, i); um[i] = i; }
        std::printf("%d %d %d %d %d %d %d\n", (int)l.size(), (int)d.size(), (int)m.size(), (int)s.size(), (int)mm.count(3),
                    (int)um.size(), (int)(live > 0 && made > 0));
        std::map<int, std::string, std::less<int>, Count<std::pair<const int, std::string> > > m2(std::move(m));
        std::printf("%d %d\n", (int)m.size(), (int)m2.size());
    }
    std::printf("%d %d\n", live, (int)(made == gone));
    std::map<int, std::unique_ptr<int> > owners;
    owners[1].reset(new int(10));
    owners.emplace(2, std::unique_ptr<int>(new int(20)));
    owners.insert(std::make_pair(3, std::unique_ptr<int>(new int(30))));
    std::map<int, std::unique_ptr<int> > moved(std::move(owners));
    std::printf("%d %d %d %d\n", *moved[1], *moved[2], *moved[3], (int)owners.size());
    std::set<Heavy> hs;
    hs.emplace(5);
    hs.emplace(3);
    hs.emplace(5);
    std::map<int, Heavy> hm;
    hm.emplace(std::piecewise_construct, std::forward_as_tuple(1), std::forward_as_tuple(7));
    std::printf("%d %d %d\n", (int)hs.size(), hm.at(1).v, Heavy::copies);
    return 0;
}
