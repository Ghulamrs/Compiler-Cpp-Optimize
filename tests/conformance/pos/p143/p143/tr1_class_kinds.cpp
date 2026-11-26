// The type-trait built-ins clang and cl share, asked of the kinds of class: [meta.unary.cat], [meta.unary.prop]
// and [meta.rel]'s is_base_of, which C++11 cannot answer in itself.
#include <cstdio>
struct Empty {};
struct EmptyD : Empty {};
struct Data { int x; };
union U { int i; float f; };
enum E { e1 };
enum class EC : short { a };
struct Poly { virtual void f(); };
void Poly::f() {}
struct PolyD : Poly {};
struct Abs { virtual void g() = 0; };
struct AbsD : Abs { void g() {} };
struct Fin final { int k; };
struct VD { virtual ~VD() {} };
struct VDD : VD {};
struct VB : virtual Empty {};
struct Priv : private Data {};
struct Two : Empty, Data {};
#define P(e) std::printf("%d", (int)(e))
int main() {
    P(__is_class(Empty)); P(__is_class(U)); P(__is_class(E)); P(__is_class(int)); P(__is_class(const Data));
    P(__is_class(Data *)); P(__is_class(Data &)); P(__is_class(int Data::*)); P(__is_class(void (Data::*)()));
    std::printf(" ");
    P(__is_union(U)); P(__is_union(Data)); P(__is_union(const U)); P(__is_union(int));
    P(__is_enum(E)); P(__is_enum(EC)); P(__is_enum(const E)); P(__is_enum(int)); P(__is_enum(U));
    std::printf(" ");
    P(__is_empty(Empty)); P(__is_empty(EmptyD)); P(__is_empty(Data)); P(__is_empty(U)); P(__is_empty(Poly));
    P(__is_empty(VB)); P(__is_empty(int)); P(__is_empty(const Empty));
    std::printf(" ");
    P(__is_polymorphic(Poly)); P(__is_polymorphic(PolyD)); P(__is_polymorphic(Abs)); P(__is_polymorphic(Data));
    P(__is_polymorphic(VB)); P(__is_polymorphic(VD)); P(__is_polymorphic(int));
    std::printf(" ");
    P(__is_abstract(Abs)); P(__is_abstract(AbsD)); P(__is_abstract(Poly)); P(__is_abstract(int));
    P(__is_final(Fin)); P(__is_final(Data)); P(__is_final(int));
    std::printf(" ");
    P(__has_virtual_destructor(VD)); P(__has_virtual_destructor(VDD)); P(__has_virtual_destructor(Poly));
    P(__has_virtual_destructor(Data)); P(__has_virtual_destructor(int));
    std::printf(" ");
    P(__is_base_of(Empty, EmptyD)); P(__is_base_of(EmptyD, Empty)); P(__is_base_of(Data, Priv));
    P(__is_base_of(Empty, VB)); P(__is_base_of(Data, Data)); P(__is_base_of(const Data, Data));
    P(__is_base_of(Data, Two)); P(__is_base_of(Empty, Two)); P(__is_base_of(int, int)); P(__is_base_of(U, U));
    P(__is_base_of(Poly, AbsD));
    std::printf("\n");
    static_assert(__is_class(Data) && !__is_union(Data) && __is_enum(EC), "in a constant expression");
    char a[__is_empty(Empty) ? 2 : 1];
    std::printf("%d\n", (int)sizeof a);
}