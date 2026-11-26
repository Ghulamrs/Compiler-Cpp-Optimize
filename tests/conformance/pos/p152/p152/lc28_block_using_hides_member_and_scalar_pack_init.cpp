#include <cstdio>
#include <queue>
#include <vector>
#include <utility>
namespace N {
struct Box {
    std::vector<int> c;
    void swap(Box &o) {
        using std::swap;
        swap(c, o.c);
    }
};
}
template <class... A> int one(A &&... a) {
    int keep(std::forward<A>(a)...);
    double none(std::forward<A>(a)...);
    return keep + (int)none;
}
template <class... A> int zero(A &&... a) {
    int keep(std::forward<A>(a)...);
    return keep;
}
int main() {
    std::queue<int> a, b;
    a.push(1); a.push(2); b.push(3);
    a.swap(b);
    N::Box x, y;
    x.c.push_back(5);
    x.swap(y);
    std::vector<int> v;
    v.emplace(v.begin(), 7);
    std::printf("%d %d %d %d %d %d\n", (int)a.size(), (int)b.size(), (int)y.c.size(), v[0], one(4), zero());
}
