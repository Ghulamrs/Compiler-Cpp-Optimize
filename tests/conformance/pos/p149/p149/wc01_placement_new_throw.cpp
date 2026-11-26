// [expr.new]/20 with [new.delete.placement]: a constructor that throws in `new (p) T` gives nothing back - the
// storage is the caller's - while `new T` frees what it allocated; neither touches the other's heap.
#include <cstdio>
#include <cstdlib>
#include <new>
static int frees = 0;
void operator delete(void *p) noexcept { frees++; std::free(p); }
struct T { int v; T(int x) : v(x) { if (x == 13) throw 1; } };
struct A { int v; A() : v(0) { throw 2; } };
int main() {
    alignas(T) unsigned char buf[sizeof(T) * 3];
    void *heap = std::malloc(sizeof(T));
    try { new (buf) T(13); } catch (int) { std::printf("c1 %d\n", frees); }
    try { new (heap) T(13); } catch (int) { std::printf("c2 %d\n", frees); }
    try { new (buf) A[2]; } catch (int) { std::printf("c3 %d\n", frees); }
    try { T *t = new T(13); (void)t; } catch (int) { std::printf("c4 %d\n", frees); }
    T *ok = new (heap) T(5);
    std::printf("%d %d\n", ok->v, frees);
    std::free(heap);
    return 0;
}
