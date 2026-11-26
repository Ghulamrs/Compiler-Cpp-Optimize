// [namespace.def]: unnamed, nested, aliases, using-directives and -declarations, inline namespaces and their ADL/lookup, qualified definition of members, ::global.
#include <cstdio>
namespace A { int v = 1; namespace B { int v = 2; } }
namespace { int anon = 3; }
namespace Alias = A::B;
namespace Lib { inline namespace V2 { int ver() { return 2; } struct T { int x; }; } namespace V1 { int ver() { return 1; } } }
namespace Lib { int viaInline() { return ver(); } }
namespace X { void f(); }
void X::f() { std::printf("X::f\n"); }
int v = 99;
namespace Deep { namespace Mid { namespace In { int g() { return 42; } } } }
using namespace Deep::Mid;
namespace Re { namespace Q { int q = 8; } }
namespace Re2 = Re::Q;
int main() {
    using A::v; using namespace A::B;
    std::printf("%d %d %d %d %d\n", v, A::B::v, anon, Alias::v, ::v);
    std::printf("%d %d %d %d\n", Lib::ver(), Lib::V2::ver(), Lib::V1::ver(), Lib::viaInline());
    Lib::T t = {5}; Lib::V2::T t2 = t; std::printf("%d\n", t2.x);
    X::f();
    std::printf("%d %d\n", In::g(), Re2::q);
    return 0;
}
