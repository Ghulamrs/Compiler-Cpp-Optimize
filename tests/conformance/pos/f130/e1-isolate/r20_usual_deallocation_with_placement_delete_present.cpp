#include <cstdio>
#include <cstdlib>
#include <cstddef>
struct S { static void *operator new(std::size_t n) { return std::malloc(n); } static void operator delete(void *p) { std::printf("del "); std::free(p); } static void *operator new(std::size_t n, int) { return std::malloc(n); } static void operator delete(void *p, int) { std::free(p); } };
int main() { S *s = new S; delete s; std::printf("\n"); return 0; }
