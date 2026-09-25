// **`delete` through a virtual destructor frees with the class's own
// `operator delete`**: the deleting destructor the vtable holds calls it,
// found through the bases as [class.free] finds it - the platform's was
// called whatever the class declared until 2026-09-14 (finding 2). And
// `new (p) T` registers nothing for destruction.
#include <stddef.h>
extern "C" int printf(const char *, ...);
extern "C" void *malloc(size_t);
extern "C" void free(void *);
inline void *operator new(size_t, void *where) throw() { return where; }
inline void operator delete(void *, void *) throw() {}
struct Base {
    int x; Base() : x(1) {} virtual ~Base() { printf("~Base\n"); }
    static void *operator new(size_t n) { printf("Base::new\n"); return malloc(n); }
    static void operator delete(void *p) { printf("Base::delete\n"); free(p); }
};
struct Der : Base {
    int y; Der() : y(2) {} ~Der() { printf("~Der\n"); }
    static void *operator new(size_t n) { printf("Der::new\n"); return malloc(n); }
    static void operator delete(void *p) { printf("Der::delete\n"); free(p); }
};
struct Plain { int v; ~Plain() { printf("~Plain %d\n", v); } };
struct NV { int a; static void operator delete(void *p) { printf("NV::delete\n"); free(p); } static void *operator new(size_t n) { return malloc(n); } };
struct NV2 : NV { };
int main() {
    Base *b = new Der;
    delete b;
    Base *b2 = new Base; delete b2;
    NV *n = new NV2; delete n;
    char buf[sizeof(Plain)];
    Plain *p = new (buf) Plain; p->v = 7;
    printf("placement done\n");
    return 0;
}
