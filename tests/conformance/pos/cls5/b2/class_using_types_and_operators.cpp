#include <cstdio>
struct B {
    typedef int value_type;
    enum { k = 4 };
    int operator()(int x) const { return x + 1; }
    B &operator=(int) { return *this; }
    int operator[](int i) const { return i * 2; }
};
struct D : B {
    using B::value_type;
    using B::operator();
    using B::operator[];
    int operator()(int x, int y) const { return x * y; }
    value_type twice(value_type v) const { return v * 2; }
};
int main() {
    D d;
    D::value_type v = d.twice(3);
    std::printf("%d %d %d %d %d\n", v, d(4), d(2, 5), d[3], (int)D::k);
    return 0;
}
