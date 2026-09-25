// **operator new and operator delete: replaced, given to a class, placed.**
// A definition of the global pair replaces the library's, being the same
// symbol; a class's own, static whether it says so or not, allocates its
// objects and a derived class's; `new (p) T` builds at p and calls nothing.
// Refused by name until 2026-09-14 (review A8).
#include <stddef.h>
// The library's placement pair, as <new> declares it, for the mangling
// oracle that has no headers for the targets it cross-compiles.
inline void *operator new(size_t, void *where) throw() { return where; }
inline void operator delete(void *, void *) throw() {}
extern "C" int printf(const char *, ...);
extern "C" void *malloc(size_t);
extern "C" void free(void *);
static int globalNews = 0, globalDeletes = 0, poolNews = 0, poolDeletes = 0;
void *operator new(size_t n) { globalNews++; return malloc(n); }
void operator delete(void *p) { globalDeletes++; free(p); }
static char pool[256];
static int poolTop = 0;
struct Pooled {
    int a, b;
    Pooled(int x) : a(x), b(x * 2) {}
    ~Pooled() { poolDeletes += 100; }
    static void *operator new(size_t n) { poolNews++; void *p = pool + poolTop; poolTop += (int)n; return p; }
    static void operator delete(void *p) { poolDeletes++; (void)p; }
};
struct Sub : Pooled { Sub() : Pooled(9) {} };
struct Plain { int v; Plain(int x) : v(x) {} };
int main() {
    int *i = new int(5);
    Plain *q = new Plain(7);
    Pooled *p = new Pooled(3);
    Sub *s = new Sub();
    char raw[sizeof(Plain)];
    Plain *placed = new (raw) Plain(11);
    int *pi = new (&i[0]) int(6);
    printf("%d %d %d %d %d %d %d\n", *i, q->v, p->a, p->b, s->a, placed->v, *pi);
    printf("%d %d %d %d %d\n", globalNews, globalDeletes, poolNews, poolDeletes, poolTop);
    delete i; delete q; delete p; delete s;
    printf("%d %d %d %d %d\n", globalNews, globalDeletes, poolNews, poolDeletes, (int)(poolTop == 2 * (int)sizeof(Pooled)));
    return 0;
}
