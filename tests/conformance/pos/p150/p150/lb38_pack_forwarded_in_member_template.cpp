// [temp.variadic]/4: a pack forwarded inside a member function template of a class template - emplace_back.
#include <cstdio>
#include <utility>
#include <new>
template <class T> struct Vec {
    T items[4]; int n = 0;
    template <class... A> void emplace_back(A &&... a) { ::new (static_cast<void *>(&items[n++])) T(std::forward<A>(a)...); }
};
struct P { int x; double y; P() : x(0), y(0) {} P(int a, double b) : x(a), y(b) {} };
template <class T, class... A> T *make(A &&... a) { return new T(std::forward<A>(a)...); }
int main() {
    Vec<P> v;
    v.emplace_back(1, 2.5);
    v.emplace_back();
    P *q = make<P>(3, 4.5);
    std::printf("%d %g %d %d %g\n", v.items[0].x, v.items[0].y, v.n, q->x, q->y);
    delete q;
    return 0;
}
