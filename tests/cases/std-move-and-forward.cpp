// std::move picks the move constructor and the move assignment; a call that
// returns T && is an xvalue; a class with both assignments takes the one the
// value's category asks for; std::reverse (the cl review's A22).
#include <utility>
#include <algorithm>
extern "C" int printf(const char *, ...);
struct Buf {
    int *p; int n;
    Buf(int k) : p(new int[k]), n(k) { for (int i = 0; i < k; ++i) p[i] = i; printf("Buf(%d)\n", k); }
    Buf(const Buf &o) : p(new int[o.n]), n(o.n) { for (int i = 0; i < n; ++i) p[i] = o.p[i]; printf("copy\n"); }
    Buf(Buf &&o) : p(o.p), n(o.n) { o.p = 0; o.n = 0; printf("move\n"); }
    Buf &operator=(Buf &&o) { delete[] p; p = o.p; n = o.n; o.p = 0; o.n = 0; printf("move=\n"); return *this; }
    Buf &operator=(const Buf &o) { if (this != &o) { delete[] p; p = new int[o.n]; n = o.n; for (int i = 0; i < n; ++i) p[i] = o.p[i]; } printf("copy=\n"); return *this; }
    ~Buf() { delete[] p; }
};
int which(int &) { return 1; }
int which(int &&) { return 2; }
int main() {
    Buf a(3);
    Buf b(std::move(a));
    Buf d(1);
    d = std::move(b);
    Buf e(2);
    e = d;
    int x = 0;
    printf("%d %d %d %d\n", a.n, b.n, d.n, e.n);
    printf("%d %d %d %d\n", which(x), which(1), which(std::move(x)), which(std::forward<int>(x)));
    int v[5] = {1, 2, 3, 4, 5};
    std::reverse(v, v + 5);
    printf("%d %d %d %d %d\n", v[0], v[1], v[2], v[3], v[4]);
    return 0;
}
