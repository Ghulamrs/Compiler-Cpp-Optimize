#include <cstdio>
#include <vector>
#include <new>
#include <utility>
#include <algorithm>
#include <functional>
#include <memory>
#include <string>
static int constructs = 0, destroys = 0;
template <class T> struct Counting {
    typedef T value_type;
    int *live;
    explicit Counting(int *l) : live(l) {}
    template <class U> Counting(const Counting<U> &o) : live(o.live) {}
    T *allocate(std::size_t n) { ++*live; return static_cast<T *>(::operator new(n * sizeof(T))); }
    void deallocate(T *p, std::size_t) { --*live; ::operator delete(p); }
    template <class U, class... A> void construct(U *p, A &&... a) { ++constructs; ::new ((void *)p) U(std::forward<A>(a)...); }
    template <class U> void destroy(U *p) { ++destroys; p->~U(); }
};
template <class T, class U> bool operator==(const Counting<T> &a, const Counting<U> &b) { return a.live == b.live; }
template <class T, class U> bool operator!=(const Counting<T> &a, const Counting<U> &b) { return a.live != b.live; }
struct Throwy {
    static int copies, moves;
    int v;
    explicit Throwy(int x) : v(x) {}
    Throwy(const Throwy &o) : v(o.v) { ++copies; }
    Throwy(Throwy &&o) : v(o.v) { ++moves; }
    Throwy &operator=(const Throwy &o) { v = o.v; return *this; }
};
int Throwy::copies = 0, Throwy::moves = 0;
struct Calm {
    static int copies, moves;
    int v;
    explicit Calm(int x) : v(x) {}
    Calm(const Calm &o) noexcept : v(o.v) { ++copies; }
    Calm(Calm &&o) noexcept : v(o.v) { ++moves; }
    Calm &operator=(const Calm &o) { v = o.v; return *this; }
};
int Calm::copies = 0, Calm::moves = 0;
int main() {
    int live = 0, other = 0;
    {
        Counting<std::string> a(&live);
        std::vector<std::string, Counting<std::string> > v(a);
        for (int i = 0; i < 20; i++) v.push_back(std::string(i % 5 + 1, (char)('a' + i % 26)));
        v.emplace_back(3, 'z');
        v.insert(v.begin() + 2, "mid");
        v.erase(v.begin() + 5);
        std::printf("%d %s %s %s %d\n", (int)v.size(), v[2].c_str(), v.back().c_str(), v.front().c_str(), live > 0);
        std::vector<std::string, Counting<std::string> > w(v);
        std::vector<std::string, Counting<std::string> > x(std::move(w), Counting<std::string>(&other));
        std::printf("%d %d %d %d\n", (int)x.size(), x[3] == v[3], other > 0, (int)(x.get_allocator() == Counting<std::string>(&other)));
        x.swap(v);
        std::printf("%d %d\n", (int)v.size(), constructs > 0 && destroys > 0);
    }
    std::printf("%d %d\n", live, other);
    {
        std::vector<Throwy> t;
        for (int i = 0; i < 40; i++) t.push_back(Throwy(i));
        int movesBefore = Throwy::moves, copiesBefore = Throwy::copies;
        for (int i = 0; i < 100; i++) t.emplace_back(i);
        std::printf("throwy regrowth: moves %d copies %d\n", Throwy::moves - movesBefore, Throwy::copies > copiesBefore);
        std::vector<Calm> c;
        for (int i = 0; i < 40; i++) c.push_back(Calm(i));
        int calmCopies = Calm::copies;
        for (int i = 0; i < 100; i++) c.emplace_back(i);
        std::printf("calm regrowth: copies %d moves %d\n", Calm::copies - calmCopies, Calm::moves > 40);
    }
    {
        std::vector<bool> b;
        for (int i = 0; i < 130; i++) b.push_back(i % 3 == 0);
        std::printf("%d %d %d %d\n", (int)b.size(), (int)std::count(b.begin(), b.end(), true), (int)b[3], (int)b[4]);
        b.flip();
        b[0] = true;
        b.insert(b.begin() + 1, 3, true);
        b.erase(b.begin() + 10, b.begin() + 20);
        std::vector<bool>::reference r0 = b[0], r1 = b[129 - 7];
        std::vector<bool>::swap(r0, r1);
        std::printf("%d %d %d %d\n", (int)b.size(), (int)std::count(b.begin(), b.end(), true), (int)b[0], (int)b.back());
        std::vector<bool> c(b), d = {true, false, true};
        std::printf("%d %d %d %d\n", (int)(c == b), (int)(d < b), (int)(std::hash<std::vector<bool> >()(c) == std::hash<std::vector<bool> >()(b)), (int)d.at(2));
        std::vector<bool>::const_iterator it = c.cbegin() + 64;
        std::printf("%d %d\n", (int)(c.end() - it), (int)*(it + 1));
        c.resize(200, true);
        c.pop_back();
        std::printf("%d %d\n", (int)c.size(), (int)std::count(c.begin(), c.end(), true));
    }
    return 0;
}
