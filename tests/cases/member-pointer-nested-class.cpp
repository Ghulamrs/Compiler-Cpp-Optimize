// A20: a pointer to a member of a nested class, in a parameter's declarator
// and in `&Nest::In::v`; the same through a namespace.
extern "C" int printf(const char *, ...);
struct Nest { struct In { int v; int f(int k) { return k + v; } }; };
namespace ns { struct S { int w; int g() { return w * 2; } }; }
int call(int (Nest::In::*m)(int), Nest::In *o) { return (o->*m)(1); }
int main() {
    Nest::In i; i.v = 2;
    int Nest::In::*pd = &Nest::In::v;
    int (Nest::In::*pf)(int) = &Nest::In::f;
    ns::S s; s.w = 4;
    int ns::S::*pw = &ns::S::w;
    int (ns::S::*pg)() = &ns::S::g;
    printf("%d %d %d %d %d\n", i.*pd, (i.*pf)(3), call(pf, &i), s.*pw, (s.*pg)());
    return 0;
}
