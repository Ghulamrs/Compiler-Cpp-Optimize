#include <cstdio>
struct V { int x, y; V operator+(const V &o) const { return {x + o.x, y + o.y}; } V operator-() const { return {-x, -y}; } V &operator+=(const V &o) { x += o.x; y += o.y; return *this; } V &operator++() { x++; return *this; } V operator++(int) { V t = *this; y++; return t; }
  int operator[](int i) const { return i ? y : x; } int &operator[](int i) { return i ? y : x; } int operator()(int a, int b = 1) const { return x * a + y * b; } bool operator==(const V &o) const { return x == o.x && y == o.y; } bool operator!() const { return x == 0 && y == 0; } V *operator->() { return this; } V &operator*() { return *this; } };
V operator*(int k, const V &v) { return {k * v.x, k * v.y}; } bool operator<(const V &a, const V &b) { return a.x < b.x; }
struct Idx { int d[3] = {1, 2, 3}; int &operator[](long i) { return d[i]; } }; struct Ptr { V v{1, 2}; V *operator->() { return &v; } };
int main() { V a{1, 2}, b{3, 4}; V c = a + b; V d = -a; a += b; V e = 2 * b; V f = a++; ++a; Idx ix; Ptr p; std::printf("%d %d %d %d %d %d %d %d %d %d %d %d %d %d\n", c.x, d.y, a.x, a.y, e.x, f.x, a[1], a(1, 2), (int)(a == b), (int)!V{0, 0}, (int)(a < b), ix[2], p->y, (*a)[0]); return 0; }
