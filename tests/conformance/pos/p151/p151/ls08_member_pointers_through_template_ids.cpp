// [expr.unary.op]/3, [dcl.mptr]: a pointer to member formed and declared through a template-id, plain and in a
// namespace, and passed to mem_fn.
#include <cstdio>
#include <functional>
#include <utility>
template <class T> struct Box { T t; T get() const { return t; } };
namespace n { template <class T> struct B2 { T t; T twice() const { return 2 * t; } }; }
int main() {
    Box<int> b = {7};
    int Box<int>::*pd = &Box<int>::t;
    int (Box<int>::*pf)() const = &Box<int>::get;
    n::B2<int> c = {5};
    int n::B2<int>::*qd = &n::B2<int>::t;
    int (n::B2<int>::*qf)() const = &n::B2<int>::twice;
    std::pair<int, int> p(3, 4);
    int std::pair<int, int>::*sd = &std::pair<int, int>::first;
    auto m = std::mem_fn(&std::pair<int, int>::second);
    std::printf("%d %d %d %d %d %d\n", b.*pd, (b.*pf)(), c.*qd, (c.*qf)(), p.*sd, m(p));
    return 0;
}
