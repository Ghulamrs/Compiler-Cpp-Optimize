// [meta.unary.prop]: is_pod, is_standard_layout, is_trivial, is_trivially_copyable, is_literal_type -
// the class rules of [class]/6-10 and [basic.types]/9-10.
#include <cstdio>
struct Pod { int a; char b; };
struct PodArr { Pod p[2]; double d; };
struct Ctor { Ctor() {} int a; };
struct CopyC { CopyC(const CopyC &) {} int a; };
struct Dtor { ~Dtor() {} int a; };
struct Assign { Assign &operator=(const Assign &) { return *this; } int a; };
struct MixedAccess { int a; private: int b; public: int get() const { return b; } };
struct SamePriv { private: int a, b; };
struct Base1 { int x; };
struct DerivedNoData : Base1 {};
struct DerivedData : Base1 { int y; };
struct EmptyB {};
struct FirstSame : EmptyB { EmptyB e; int z; };
struct Virt { virtual void f() {} };
struct Defaulted { Defaulted() = default; int a; };
struct WithInit { int a = 1; };
struct HasCtorMember { Ctor c; };
struct Lit { constexpr Lit(int v) : v(v) {} int v; };
struct NotLit { NotLit(int v) : v(v) {} int v; };
struct LitDtor { constexpr LitDtor() : v(0) {} ~LitDtor() {} int v; };
struct Agg { int a; double b; };
union UPod { int i; char c; };
enum E { e1 };
#define P(e) std::printf("%d", (int)(e))
#define ROW(T) P(__is_pod(T)); P(__is_standard_layout(T)); P(__is_trivial(T)); P(__is_trivially_copyable(T)); P(__is_literal_type(T)); std::printf(" ")
int main() {
    ROW(int); ROW(const int); ROW(int *); ROW(int &); ROW(E); ROW(int[3]); ROW(int Pod::*);
    std::printf("\n");
    ROW(Pod); ROW(PodArr); ROW(Pod[2]); ROW(const Pod); ROW(Ctor); ROW(CopyC); ROW(Dtor); ROW(Assign);
    std::printf("\n");
    ROW(MixedAccess); ROW(SamePriv); ROW(DerivedNoData); ROW(DerivedData); ROW(FirstSame);
    std::printf("\n");
    ROW(Virt); ROW(Defaulted); ROW(WithInit); ROW(HasCtorMember);
    std::printf("\n");
    ROW(Lit); ROW(NotLit); ROW(LitDtor); ROW(Agg); ROW(UPod);
    std::printf("\n");
}