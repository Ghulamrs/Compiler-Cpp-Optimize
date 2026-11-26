// [namespace.qual], [basic.lookup.argdep] and [over.match.best]/1: a qualified call looks only in the namespace
// it names; an unqualified one adds its arguments' namespaces; a non-template wins a tie with a specialization.
#include <cstdio>
#include <utility>
namespace other { template <class T> void f(T &) { std::printf("other::f<T>\n"); } }
namespace user {
    struct U { int v; };
    void f(U &) { std::printf("user::f\n"); }
    void swap(U &a, U &b) { std::printf("user::swap\n"); int t = a.v; a.v = b.v; b.v = t; }
}
namespace lib { template <class T> void swap(T &, T &) { std::printf("lib::swap<T>\n"); } }
using namespace other;
int main() {
    user::U u = {1}, v = {2};
    user::f(u);
    f(u);
    other::f(u);
    user::swap(u, v);
    { using lib::swap; swap(u, v); }
    { using std::swap; swap(u, v); }
    std::swap(u, v);
    int x = 3, y = 4;
    { using std::swap; swap(x, y); }
    std::printf("%d %d %d %d\n", u.v, v.v, x, y);
    return 0;
}
