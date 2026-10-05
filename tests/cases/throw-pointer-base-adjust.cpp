// **A pointer thrown as a derived class and caught as a pointer to a base at
// a non-zero offset** - [except.handle]/3 with [conv.ptr]/3: the handler's
// pointer is adjusted to the base subobject, so `p->b` reads B's member and
// not A's, and the adjustment is the base's offset. `struct D : A, B` thrown
// as `D *`, caught as `B *`, with `A *`, `D *`, `const B *` and a null beside it.
extern "C" int printf(const char *, ...);
struct A { int a; A(int v); };
struct B { int b; B(int v); };
struct D : A, B { int d; D(int v); };
A::A(int v) : a(v) {}                 // out of line: inline, clang emits C2 alone on
B::B(int v) : b(v) {}                 // x86_64-linux and names.sh reports emission
D::D(int v) : A(v), B(v + 1), d(v + 2) {}
static D dobj(1);
int asB() {
    try { throw &dobj; }
    catch (B *p) { printf("B* %d %d\n", p->b, (int)((char *)p - (char *)&dobj)); return 1; }
    return 0;
}
int asA() {
    try { throw &dobj; }
    catch (A *p) { printf("A* %d %d\n", p->a, (int)((char *)p - (char *)&dobj)); return 1; }
    return 0;
}
int asD() {
    try { throw &dobj; }
    catch (D *p) { printf("D* %d %d\n", p->d, (int)((char *)p - (char *)&dobj)); return 1; }
    return 0;
}
int constB() {
    try { throw &dobj; }
    catch (const B *p) { printf("const B* %d %d\n", p->b, (int)((const char *)p - (const char *)&dobj)); return 1; }
    return 0;
}
int nullB() {
    try { throw (D *)0; }
    catch (B *p) { printf("B* null %d\n", p == 0); return 1; }
    return 0;
}
int main() {
    printf("%d %d %d %d %d\n", asB(), asA(), asD(), constB(), nullB());
    return 0;
}
