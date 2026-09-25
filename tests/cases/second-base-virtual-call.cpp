// **A virtual function of a second base, called through the derived object
// and not overridden.** `c.g()` where g is B's and C : A, B overrides
// nothing: the slot is B's, not C's - C's table holds A's chain and C's own
// new slots and nothing of B's - so the call reads the vptr of the B
// subobject at its own offset and indexes B's table. cxx1 looked only in C's
// table and refused the call as "virtual but has no vtable slot" until
// 2026-09-15, when the virtual-inheritance round met the same shape through
// a virtual base. x86_64-windows refuses the class itself (mi-override-implied
// records why); the Itanium targets and the C6000 compile it.
extern "C" int printf(const char *, ...);
struct A { virtual int a() { return 1; } };
struct B { virtual int g() { return 2; } };
struct C : A, B {};
struct E : A, B { int g() { return 3; } };
int main() {
    C c; E e; B *pb = &e;
    printf("%d %d %d %d\n", c.g(), c.a(), e.g(), pb->g());
    return 0;
}
