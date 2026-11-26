#include <utility>
#include <new>
#include <cstdio>
template <class T> struct S {
    T buf[4];
    int n;
    S() : n(0) {}
    T *place(int i) { return buf + i; }
    template <class... A> void inner(A &&... a) {
        ::new (static_cast<void *>(place(n))) T(std::forward<A>(a)...);
        ++n;
    }
    template <class... A> void outer(int at, A &&... a) { inner(std::forward<A>(a)...); n += at; }
};
template <class T> struct U {
    T buf[4];
    template <class... A> void one(A &&... a) { ::new (static_cast<void *>(buf)) T(std::forward<A>(a)...); }
};
int main() {
    U<int> u;
    u.one(7);
    S<int> s;
    s.inner(3);
    s.outer(0, 4);
    std::printf("%d %d %d %d\n", u.buf[0], s.buf[0], s.buf[1], s.n);
}
