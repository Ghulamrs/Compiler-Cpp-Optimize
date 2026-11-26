// [except.spec]/8: a type is allowed where a handler of a listed type would match it - a derived class, a derived pointer, a qualification conversion, a try inside
#include <cstdio>
struct B { virtual ~B() {} virtual const char *n() const { return "B"; } };
struct D : B { const char *n() const { return "D"; } };
D dObj;
char text[] = "text";
void byBase() throw(B) { throw D(); }
void byPointer() throw(B *) { throw &dObj; }
void byConst() throw(const char *) { throw text; }
void inside() throw(int) {
    try { throw 2.5; } catch (double d) { throw static_cast<int>(d * 2); }
}
int main() {
    try { byBase(); } catch (B &b) { std::printf("%s ", b.n()); }
    try { byPointer(); } catch (B *p) { std::printf("%s ", p->n()); }
    try { byConst(); } catch (const char *s) { std::printf("%s ", s); }
    try { inside(); } catch (int v) { std::printf("%d\n", v); }
    return 0;
}
