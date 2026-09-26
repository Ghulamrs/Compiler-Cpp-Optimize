// A class's own `operator new[]` and `operator delete[]` ([class.free]/2, which
// `new T[n]` and `delete[] p` reach), a placement form with more than one
// argument ([expr.new]/14, class first and then the global one), and
// [expr.new]/20: a constructor that throws inside a new-expression hands the
// storage back through the matching deallocation function. The counts are what
// the program's own allocation functions saw, so elision cannot move them.
#include <cstdlib>
#include <cstdio>

static int arrNew = 0, arrDel = 0, sclNew = 0, sclDel = 0, placed = 0, gplaced = 0;
static void *lastArr = 0, *lastScl = 0;

struct S {
    int v;
    S() : v(7) {}
    ~S() {}
    static void *operator new[](std::size_t n) { arrNew++; lastArr = std::malloc(n); return lastArr; }
    static void operator delete[](void *p) { arrDel++; if (p == lastArr) std::printf("same-array "); std::free(p); }
    static void *operator new(std::size_t n) { sclNew++; lastScl = std::malloc(n); return lastScl; }
    static void operator delete(void *p) { sclDel++; if (p == lastScl) std::printf("same-scalar "); std::free(p); }
};

struct Throws {
    int v;
    Throws(int x) : v(x) { if (x < 0) throw x; }
    Throws() : v(0) { throw 5; }
    static void *operator new(std::size_t n) { sclNew++; lastScl = std::malloc(n); return lastScl; }
    static void operator delete(void *p) { sclDel++; if (p == lastScl) std::printf("freed "); std::free(p); }
    static void *operator new[](std::size_t n) { arrNew++; lastArr = std::malloc(n); return lastArr; }
    static void operator delete[](void *p) { arrDel++; if (p == lastArr) std::printf("freed-array "); std::free(p); }
};

struct P {
    int a, b;
    P(int x, int y) : a(x), b(y) {}
    static void *operator new(std::size_t n, int tag, double d) { placed += tag + (int)d; return std::malloc(n); }
    static void operator delete(void *p) { std::free(p); }
};

struct G { int z; G(int q) : z(q) {} };
void *operator new(std::size_t n, int tag, const char *why) {
    gplaced += tag;
    std::printf("[%s] ", why);
    return std::malloc(n);
}

// The throwing constructions in functions of their own, and the two `try`s in
// a third: x86_64-windows refuses a `try` beside a cleanup region in one
// function, and every `new` of a class whose constructor may throw opens one.
static void makeScalar() { Throws *t = new Throws(-1); (void)t; }
static void makeArray() { Throws *t = new Throws[2]; (void)t; }
static int catching() {
    int caught = 0;
    try { makeScalar(); } catch (int e) { caught = e; }
    std::printf("%d %d %d\n", caught, sclNew, sclDel);
    try { makeArray(); } catch (int e) { caught = e; }
    std::printf("%d %d %d\n", caught, arrNew, arrDel);
    return caught;
}

int main() {
    S *a = new S[4];
    // The cookie in front of the array is an ABI fact per target, so only its side is printed.
    std::printf("%d %d %d\n", arrNew, a[3].v, (int)((char *)a > (char *)lastArr));
    delete[] a;
    std::printf("%d %d\n", arrDel, sclNew);
    S *one = new S;
    delete one;
    std::printf("%d %d\n", sclNew, sclDel);

    P *p = new (3, 4.5) P(1, 2);
    std::printf("%d %d %d\n", placed, p->a, p->b);
    delete p;
    G *g = new (10, "global") G(9);
    std::printf("%d %d\n", gplaced, g->z);
    delete g;

    int caught = catching();
    Throws *ok = new Throws(1);
    std::printf("%d %d %d\n", ok->v, sclNew, sclDel);
    delete ok;

    const char *const fixed = "fixed";
    const char *loose = "loose";
    const char *pick = caught == 5 ? fixed : loose;
    std::printf("%s %s\n", pick, caught ? loose : fixed);
    return 0;
}
