// [basic.lookup.argdep]/2: a class template specialization's associated namespaces are the template's and every
// type argument's, and a class's bases' - each adds its functions to an unqualified call.
#include <cstdio>
namespace A { struct X {}; int who(int) { return -1; } }
namespace B { template <class T> struct W {}; struct Base {}; }
namespace C { struct D : B::Base {}; }
namespace A { int who(B::W<X>) { return 1; } }
namespace B { int base(const Base &) { return 2; } template <class T> int tmpl(const W<T> &) { return 3; } }
namespace E { template <class T, class U> struct Pair {}; }
namespace F { struct Y {}; int pairUp(E::Pair<int, Y>) { return 4; } }
int main() {
    B::W<A::X> w;
    C::D d;
    E::Pair<int, F::Y> p;
    std::printf("%d %d %d %d\n", who(w), base(d), tmpl(w), pairUp(p));
    return 0;
}
