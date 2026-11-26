#include <cstdio>
#include <cstdlib>
#include <cstddef>
#include <new>
struct S { int v; S(int v) : v(v) {} static void *operator new(std::size_t n, int tag) { std::printf("tag%d ", tag); return std::malloc(n); } static void operator delete(void *p, int) { std::printf("tagdel "); std::free(p); } static void *operator new(std::size_t n) { return std::malloc(n); } static void operator delete(void *p) { std::free(p); } };
struct Boom { Boom() { throw 1; } static void *operator new(std::size_t n, int) { std::printf("bnew "); return std::malloc(n); } static void operator delete(void *p, int) { std::printf("bdel "); std::free(p); } };
int main() { S *s = new (5) S(1); std::printf("%d ", s->v); delete s; try { new (7) Boom; } catch (int) { std::printf("caught "); } alignas(S) char buf[sizeof(S)]; S *t = ::new (buf) S(2); std::printf("%d\n", t->v); t->~S(); return 0; }
