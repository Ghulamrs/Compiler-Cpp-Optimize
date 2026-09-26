// `typeid` on every target, comparisons only - what it prints is the same on
// both ABIs, where typeid.cpp prints the names the Itanium object carries and
// so runs on the Itanium targets and the C6000. On x86_64-windows a static
// `typeid(T)` names the type descriptor `??_R0...@8` - a class's is the one
// its vftable's locator already points at, a fundamental type's and a
// pointer's are emitted for it, a pointer keeping its pointee's const
// (`??_R0PEBD@8`) where a throw's record drops it - and a polymorphic glvalue
// asks the runtime, `__RTtypeid(p)`, one argument, which throws bad_typeid for
// a null. <typeinfo> then reads the object through the vcruntime's own three
// functions. Measured from clang -target x86_64-pc-windows-msvc, 2026-09-26;
// clang's output, and the Windows box runs it.
#include <typeinfo>
extern "C" int printf(const char *, ...);
struct B { virtual ~B() {} int b; };
struct D : B { int d; };
struct Plain { int p; };
struct Tmp { ~Tmp() { printf("~Tmp "); } };
B *pick(const Tmp &, B *b) { return b; }
int side = 0;
B *make(int k) { side++; if (k) return new D; return new B; }
static int startsWithDot(const char *s) { return s[0] == '.'; }
int main() {
    B bobj; D dobj;
    B &ref = dobj;
    B *pb = &dobj;
    const std::type_info &ti = typeid(dobj);
    printf("%d %d %d %d\n", typeid(int) == typeid(int), typeid(int) == typeid(unsigned), typeid(dobj) == typeid(D), typeid(bobj) == typeid(D));
    printf("%d %d %d %d\n", typeid(*pb) == typeid(D), typeid(ref) == typeid(D), typeid(pb) == typeid(B *), typeid(*pb) != typeid(B));
    printf("%d %d %d %d\n", ti == typeid(D), typeid(Plain) == typeid(Plain), typeid(char) != typeid(signed char), typeid(const int) == typeid(int));
    printf("%d %d %d\n", typeid(const char *) != typeid(char *), typeid(int *) == typeid(int *), typeid(D).name() != 0);
    printf("%d %d %d\n", typeid(*make(1)) == typeid(D), typeid(make(0)) == typeid(B *), side);
    printf("%d %d\n", typeid(double).before(typeid(int)) != typeid(int).before(typeid(double)), typeid(D).hash_code() == typeid(dobj).hash_code());
    printf("%d ", typeid(*pick(Tmp(), &dobj)) == typeid(D));
    printf("| %d\n", typeid(pick(Tmp(), &dobj)) == typeid(B *));
    // Whether the name starts with a dot says which ABI's object this is, and
    // neither is wrong; what is pinned is that name() answers at all.
    printf("%d\n", startsWithDot(typeid(int).name()) + startsWithDot(typeid(D).name()) >= 0);
    return 0;
}
