// `dynamic_cast` to a reference - [expr.dynamic.cast]/9: the same question
// the pointer form asks, and a failure has no null to answer with, so it
// throws std::bad_cast, which <typeinfo> declares.
#include <typeinfo>
extern "C" int printf(const char *, ...);

struct B { virtual ~B() {} int b; };
struct D : B { int d; };
struct E : B { int e; };
struct F : D { int f; };

static int caught = 0;

static int probe(B &r) {
    try {
        E &re = dynamic_cast<E &>(r);
        return re.e;
    } catch (std::bad_cast &) {
        caught++;
        return -1;
    }
}

int main() {
    F f;
    f.b = 1;
    f.d = 2;
    f.f = 3;
    B &rb = f;
    D &rd = dynamic_cast<D &>(rb);
    printf("down %d %d\n", rd.b, rd.d);
    const B &cb = f;
    const F &cf = dynamic_cast<const F &>(cb);
    printf("const %d\n", cf.f);
    B &up = dynamic_cast<B &>(rd);
    printf("up %d\n", up.b);
    int r = probe(rb);
    printf("failed %d caught %d\n", r, caught);
    try {
        D d;
        B &other = d;
        F &bad = dynamic_cast<F &>(other);
        printf("not reached %d\n", bad.f);
    } catch (std::exception &) {
        printf("as std::exception\n");
    }
    E e;
    e.e = 9;
    r = probe(e);
    printf("matched %d caught %d\n", r, caught);
    return 0;
}
