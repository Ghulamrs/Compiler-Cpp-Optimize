// [temp.arg.type]/2 in C++11: an unnamed or local class may be a template argument; each unnamed class is a
// type of its own, its specializations kept apart from another's.
#include <cstdio>
template <class T> int sz(T) { return (int)sizeof(T); }
template <class T> struct W { T t; int get() const { return (int)sizeof(t); } };
struct { int a; int b; } anon_s;
struct { char c; } second_s;
namespace ns { struct { short s[3]; } inner; }
int main() {
    struct Local { int x, y, z; };
    Local l = {1, 2, 3};
    W<decltype(anon_s)> w;
    w.t.a = 5;
    W<Local> wl = {l};
    std::printf("%d %d %d %d\n", sz(l), sz(anon_s), sz(second_s), sz(ns::inner));
    std::printf("%d %d %d %d\n", w.get(), w.t.a, wl.t.z, W<decltype(second_s)>().get());
    return 0;
}
