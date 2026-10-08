// `dynamic_cast` across a class with more than one base - the runtime walks
// the __vmi_class_type_info the class carries: a downcast to the class, a
// cross cast from one base to its sibling, a diamond through virtual bases,
// and a failure answering null.
extern "C" int printf(const char *, ...);

struct A { virtual ~A() {} int a; };
struct B { virtual ~B() {} int b; };
struct C : A, B { int c; };
struct Other : B { int o; };

struct V { virtual ~V() {} int v; };
struct L : virtual V { int l; };
struct R : virtual V { int r; };
struct Dia : L, R { int d; };

int main() {
    C c;
    c.a = 1;
    c.b = 2;
    c.c = 3;
    B *pb = &c;
    C *pc = dynamic_cast<C *>(pb);
    printf("down %d %d\n", pc == &c, pc->c);
    A *pa = dynamic_cast<A *>(pb);
    printf("cross %d %d\n", pa == static_cast<A *>(&c), pa->a);
    Other o;
    B *po = &o;
    printf("null %d %d\n", dynamic_cast<C *>(po) == 0, dynamic_cast<A *>(po) == 0);

    Dia d;
    d.v = 4;
    d.l = 5;
    d.r = 6;
    d.d = 7;
    V *pv = &d;
    Dia *pd = dynamic_cast<Dia *>(pv);
    printf("diamond %d %d\n", pd == &d, pd->d);
    L *pl = dynamic_cast<L *>(pv);
    R *pr = dynamic_cast<R *>(pl);
    printf("sides %d %d\n", pl->l, pr->r);
    return 0;
}
