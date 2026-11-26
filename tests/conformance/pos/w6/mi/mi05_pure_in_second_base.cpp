// [class.abstract]: a pure virtual of the second base overridden in the derived class, called through that base
#include <cstdio>
struct Shape { virtual double area() const = 0; virtual ~Shape() {} };
struct Named { virtual const char *name() const = 0; virtual ~Named() {} };
struct Square : Shape, Named { double s; Square(double s) : s(s) {} double area() const { return s * s; } const char *name() const { return "square"; } };
void show(const Named &n, const Shape &sh) { std::printf("%s %g\n", n.name(), sh.area()); }
int main() { Square q(3); show(q, q); Named *pn = new Square(2); std::printf("%s\n", pn->name()); delete pn; return 0; }
