// A reference to a function as a data member - [class.base.init], [dcl.init.ref].
// It is bound in the mem-initialiser as any reference member is, and calling it
// by `s.r(...)`, `p->r(...)` or the bare name inside a member is an ordinary call.
extern "C" int printf(const char *, ...);

int add(int a, int b) { return a + b; }
int mul(int a, int b) { return a * b; }
static int neg(int a) { return -a; }

struct S {
    int (&r)(int, int);
    S(int (&f)(int, int));
    int call(int a, int b) { return r(a, b); }
    int viaThis(int a, int b) { return this->r(a, b); }
    int twice(int a) const { return r(r(a, a), a); }
};

struct Two {
    int (&op)(int, int);
    int (&un)(int);
    int k;
    Two(int (&o)(int, int), int (&u)(int), int k0);
    int run(int a) { return un(op(a, k)); }
};

// Out of line, so that clang emits C1 as well as C2 on x86_64-linux and the names compare.
S::S(int (&f)(int, int)) : r(f) {}
Two::Two(int (&o)(int, int), int (&u)(int), int k0) : op(o), un(u), k(k0) {}

// A namespace-scope function of the member's name is hidden by the member.
int r(int, int) { return 999; }

int main() {
    S s(add);
    S m(mul);
    printf("%d %d %d\n", s.r(2, 3), m.r(2, 3), s.call(4, 5));
    printf("%d %d\n", m.viaThis(6, 7), m.twice(3));
    S *p = &m;
    printf("%d\n", p->r(8, 9));
    Two t(mul, neg, 10);
    printf("%d %d\n", t.run(4), t.op(1, 2));
    const S cs(add);
    printf("%d %d\n", cs.r(20, 22), cs.twice(5));
    S copy(s);
    printf("%d %d\n", copy.r(1, 1), r(1, 1));
    int (&local)(int, int) = mul;
    S fromLocal(local);
    printf("%d %d\n", local(3, 4), fromLocal.call(5, 6));
    return 0;
}
