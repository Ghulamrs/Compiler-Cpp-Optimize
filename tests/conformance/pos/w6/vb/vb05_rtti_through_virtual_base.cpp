// [expr.dynamic.cast]/8, [expr.typeid]/2: casts and typeid from a virtual base and across the branches of a diamond
#include <cstdio>
#include <typeinfo>
struct I { virtual int id() const = 0; virtual ~I() {} };
struct IA : virtual I { int id() const { return 1; } };
struct IB : virtual I {};
struct IC : IA, IB {};
int main() {
    IC c; I &ri = c; IB *pb = &c;
    std::printf("%d %d %d %d %d %d\n", ri.id(), dynamic_cast<IC *>(&ri) == &c, dynamic_cast<IB *>(&ri) == pb,
                dynamic_cast<IA *>(pb) == static_cast<IA *>(&c), dynamic_cast<void *>(pb) == (void *)&c, typeid(ri) == typeid(IC));
    return 0;
}
