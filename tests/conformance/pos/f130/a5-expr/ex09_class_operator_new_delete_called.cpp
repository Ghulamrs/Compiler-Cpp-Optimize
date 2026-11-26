#include <cstdio>
#include <cstdlib>
#include <cstddef>
struct T { int v; static void *operator new(std::size_t n) { std::printf("new%d ", (int)n); return std::malloc(n); } static void operator delete(void *p) { std::printf("del "); std::free(p); }
  static void *operator new[](std::size_t n) { std::printf("new[]%d ", (int)(n >= 3 * sizeof(int))); return std::malloc(n); } static void operator delete[](void *p) { std::printf("del[] "); std::free(p); } };
int main() { T *p = new T; delete p; T *q = new T[3]; delete[] q; std::printf("\n"); return 0; }
