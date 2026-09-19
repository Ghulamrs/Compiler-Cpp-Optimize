// A18: `c.A::x` names the member of the A subobject; `c.A::get()` calls A's
// function without dispatch; a class's own name qualifies its own member.
// A25: a member in two base subobjects is ambiguous unqualified, and a member
// the derived class writes itself hides both.
extern "C" int printf(const char *, ...);
struct A { int x; virtual int get() const { return x; } static int n; };
int A::n = 7;
struct B { int x; int y; };
struct C : A, B { int y; int get() const { return 100; } };
struct D : A { int z; };
int main() {
    C c; c.A::x = 1; c.B::x = 2; c.B::y = 3; c.y = 4; c.C::y = 5;
    C *p = &c;
    D d; d.x = 8; d.A::x = 9;
    printf("%d %d %d %d %d %d %d %d %d\n", c.A::x, p->B::x, c.B::y, c.y, c.get(),
           c.A::get(), p->A::n, d.x, d.A::x);
    return 0;
}
