// static_cast from a base to a derived class - [expr.static.cast]/2 (a
// reference) and /11 (a pointer): the inverse of the base conversion, the
// pointer moved back by the base's offset, a null pointer kept null.
extern "C" int printf(const char *, ...);

struct A { int a; };
struct B { int b; };
struct D : A, B { int d; };
struct E : D { int e; };

struct Base { int v; };
struct Mid : Base { int m; };
struct Leaf : Mid { int l; };

int main() {
    D d;
    d.a = 1; d.b = 2; d.d = 3;
    A *pa = &d;
    B *pb = &d;
    // The second base sits past the first, so the pointer walks back.
    D *fromB = static_cast<D *>(pb);
    D *fromA = static_cast<D *>(pa);
    printf("pointer %d %d %d\n", fromB->d, fromA->d, fromB == fromA);

    // A reference, through either base.
    A &ra = d;
    B &rb = d;
    D &dA = static_cast<D &>(ra);
    D &dB = static_cast<D &>(rb);
    dB.d = 30;
    printf("reference %d %d %d\n", dA.d, dB.b, &dA == &d);

    // Null stays null, whatever the offset.
    B *nb = 0;
    D *nd = static_cast<D *>(nb);
    printf("null %d\n", nd == 0);

    // Two levels at once: E through its B, which is inside its D.
    E e;
    e.a = 10; e.b = 20; e.d = 30; e.e = 40;
    B *eb = &e;
    E *pe = static_cast<E *>(eb);
    printf("two levels %d %d %d\n", pe->e, pe->b, pe == &e);

    // A chain of first bases, every one at offset 0.
    Leaf leaf;
    leaf.v = 5; leaf.m = 6; leaf.l = 7;
    Base *bp = &leaf;
    Leaf *lp = static_cast<Leaf *>(bp);
    Mid &mr = static_cast<Mid &>(*bp);
    printf("chain %d %d %d\n", lp->l, mr.m, lp == &leaf);

    // A const base pointer gives a const derived pointer.
    const B *cb = &d;
    const D *cd = static_cast<const D *>(cb);
    printf("const %d\n", cd->d);
    return 0;
}
