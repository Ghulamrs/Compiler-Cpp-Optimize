// Virtual calls through a base pointer, alternating two classes.
#include <stdio.h>
struct Shape { virtual ~Shape() {} virtual int area() const = 0; };
struct Sq : Shape { int s; Sq(int x) : s(x) {} int area() const { return s * s; } };
struct Rc : Shape { int w, h; Rc(int a, int b) : w(a), h(b) {} int area() const { return w * h; } };
static long long virt(int n)
{
    Sq a(3);
    Rc b(4, 5);
    Shape *p[2] = {&a, &b};
    long long s = 0;
    for (int i = 0; i < n; ++i) s += p[i & 1]->area();
    return s;
}
int main() { printf("virt %lld\n", virt(20000)); return 0; }
