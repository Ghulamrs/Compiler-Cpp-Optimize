// A member class template found in a base class, bare inside the derived class and qualified through it.
#include <cstdio>
struct B { template <class T> struct Holder { T h; }; protected: template <class T> struct Prot { T p; }; };
struct D : B { Holder<int> hi; Prot<char> pc; char get() const { return pc.p; } };
int main() {
    D d; d.hi.h = 5; d.pc.p = 'y';
    D::Holder<char> c; c.h = 'x';
    std::printf("%d %c %c\n", d.hi.h, c.h, d.get());
    return 0;
}
