// R27: [expr.new]/20 - a placement new whose constructor throws frees through the matching placement
// operator delete, its arguments evaluated once; [expr.new]/9 - ::new and ::delete pass the class's over.
#include <cstdio>
#include <cstdlib>
#include <cstddef>
#include <new>
struct Boom {
    int v;
    Boom(int x) : v(x) { if (x < 0) throw x; }
    static void *operator new(std::size_t n, int tag) { std::printf("new%d ", tag); return std::malloc(n); }
    static void operator delete(void *p, int tag) { std::printf("del%d ", tag); std::free(p); }
    static void *operator new(std::size_t n) { std::printf("cls "); return std::malloc(n); }
    static void operator delete(void *p) { std::printf("clsdel "); std::free(p); }
};
static int counter = 0;
int next() { return ++counter; }
int main() {
    try { new (next()) Boom(-1); } catch (int e) { std::printf("caught%d ", e); }
    Boom *b = new (next()) Boom(3);
    std::printf("%d ", b->v);
    delete b;
    Boom *g = ::new Boom(4);
    std::printf("%d ", g->v);
    ::delete g;
    Boom *c = new Boom(5);
    std::printf("%d ", c->v);
    delete c;
    std::printf("%d\n", counter);
    return 0;
}