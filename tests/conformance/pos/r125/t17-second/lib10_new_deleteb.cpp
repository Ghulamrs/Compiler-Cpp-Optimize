// [expr.new], [expr.delete]: new/delete of scalars, arrays with non-trivial dtors (count), placement new, nothrow, class operator new/delete with size, global replacement? (skipped), delete of null, new of arrays of arrays, alignment of new'd memory.
#include <cstdio>
#include <new>
#include <cstdlib>
struct T { static int live; int v; T() : v(1) { ++live; } T(int v) : v(v) { ++live; } ~T() { --live; } };
int T::live = 0;
struct Big { alignas(16) double d[2]; static void *operator new(size_t n) { std::printf("new%d ", (int)n); return std::malloc(n); } static void operator delete(void *p) { std::printf("del "); std::free(p); } static void *operator new[](size_t n) { std::printf("new[]%d ", (int)n); return std::malloc(n); } static void operator delete[](void *p) { std::printf("del[] "); std::free(p); } };
int main() {
    int *p = new int(5); int *arr = new int[4](); std::printf("%d %d %d\n", *p, arr[3], (int)(((unsigned long long)p) % 8)); delete p; delete[] arr;
    T *ts = new T[10]; std::printf("%d %d ", T::live, ts[9].v); delete[] ts; std::printf("%d\n", T::live);
    T *one = new T(7); std::printf("%d ", one->v); delete one; std::printf("%d\n", T::live);
    alignas(T) char buf[sizeof(T)]; T *pl = new (buf) T(9); std::printf("%d %d ", pl->v, T::live); pl->~T(); std::printf("%d\n", T::live);
    int *nt = new (std::nothrow) int(3); std::printf("%d\n", *nt); delete nt;
    Big *b = new Big; delete b; Big *bs = new Big[2]; delete[] bs; std::printf("\n");
    T *null = 0; delete null; delete[] null; std::printf("ok\n");
    double *d = new double; std::printf("%d\n", (int)(((unsigned long long)d) % 8)); delete d;
    const int *ci = new const int(4); std::printf("%d\n", *ci); delete ci;
    return 0;
}
