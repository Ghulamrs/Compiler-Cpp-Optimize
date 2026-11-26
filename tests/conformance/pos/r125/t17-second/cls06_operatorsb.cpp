// [over.oper]: member and non-member operators, operator(), operator[], operator->, conversion operators (explicit and implicit), pre/post increment, comparison, stream-like chaining, operator new/delete for a class.
#include <cstdio>
#include <cstdlib>
struct V2 {
    int x, y;
    V2(int x = 0, int y = 0) : x(x), y(y) {}
    V2 operator+(const V2 &o) const { return V2(x + o.x, y + o.y); }
    V2 &operator+=(const V2 &o) { x += o.x; y += o.y; return *this; }
    V2 operator-() const { return V2(-x, -y); }
    bool operator==(const V2 &o) const { return x == o.x && y == o.y; }
    int operator[](int i) const { return i ? y : x; }
    int &operator[](int i) { return i ? y : x; }
    V2 &operator++() { ++x; return *this; }
    V2 operator++(int) { V2 t = *this; ++x; return t; }
    int operator()(int k) const { return x * k + y; }
    explicit operator bool() const { return x || y; }
    operator int() const { return x * 10 + y; }
};
V2 operator*(int k, const V2 &v) { return V2(k * v.x, k * v.y); }
bool operator<(const V2 &a, const V2 &b) { return a.x < b.x; }
struct Ptr { V2 v; V2 *operator->() { return &v; } V2 &operator*() { return v; } };
struct Out { Out &operator<<(int n) { std::printf("<%d>", n); return *this; } Out &operator<<(const char *s) { std::printf("<%s>", s); return *this; } };
struct Alloc { static int news; void *operator new(size_t n) { ++news; return std::malloc(n); } void operator delete(void *p) { --news; std::free(p); } int v; };
int Alloc::news = 0;
int main() {
    V2 a(1, 2), b(3, 4);
    V2 c = a + b; c += a; V2 d = -c; V2 e = 2 * a;
    std::printf("%d %d %d %d %d %d\n", c.x, c.y, d.x, d.y, e.x, e.y);
    std::printf("%d %d %d %d\n", a == V2(1, 2), a < b, a[0], a[1]);
    a[1] = 9; std::printf("%d\n", a.y);
    V2 f = a++; V2 g = ++a; std::printf("%d %d %d\n", f.x, g.x, a(2));
    if (a) std::printf("truthy "); if (!V2()) std::printf("falsy\n");
    int conv = b; std::printf("%d\n", conv);
    Ptr p; p->x = 5; (*p).y = 6; std::printf("%d %d\n", p.v.x, p.v.y);
    Out() << 1 << "two" << 3; std::printf("\n");
    Alloc *al = new Alloc; std::printf("%d ", Alloc::news); delete al; std::printf("%d\n", Alloc::news);
    return 0;
}
