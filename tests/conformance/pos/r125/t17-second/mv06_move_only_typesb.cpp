// Move-only types through functions, returned, stored in structs, swapped, in arrays via move, unique_ptr from the shipped <memory> as the canonical move-only type.
#include <cstdio>
#include <memory>
#include <utility>
struct Res { int *p; explicit Res(int v) : p(new int(v)) {} Res(Res &&o) : p(o.p) { o.p = 0; } Res &operator=(Res &&o) { if (this != &o) { delete p; p = o.p; o.p = 0; } return *this; } ~Res() { delete p; } Res(const Res &) = delete; Res &operator=(const Res &) = delete; };
Res makeRes(int v) { return Res(v); }
Res passThrough(Res r) { return r; }
struct Owner { Res r; Owner(int v) : r(v) {} };
std::unique_ptr<int> makeU(int v) { return std::unique_ptr<int>(new int(v)); }
int main() {
    Res a = makeRes(1); Res b = passThrough(std::move(a));
    std::printf("%d %d\n", a.p == 0, *b.p);
    Owner o(2); Owner o2 = std::move(o); std::printf("%d %d\n", o.r.p == 0, *o2.r.p);
    Res c(4);
    std::unique_ptr<int> u = makeU(5); std::unique_ptr<int> u2 = std::move(u);
    std::printf("%d %d\n", u.get() == 0, *u2);
    u = std::move(u2); std::printf("%d %d\n", *u, u2.get() == 0);
    std::unique_ptr<Owner> po(new Owner(6)); std::printf("%d\n", *po->r.p);
    Res tmp = std::move(b); b = std::move(c); c = std::move(tmp); std::printf("%d %d\n", *b.p, *c.p);
    return 0;
}
