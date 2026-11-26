#include <cstdio>
#include <cstdlib>
#include <cstddef>
struct Boom { Boom() { throw 1; } static void *operator new(std::size_t n) { std::printf("new "); return std::malloc(n); } static void operator delete(void *p) { std::printf("delete "); std::free(p); } };
struct Elem { static int made; Elem() { if (++made == 3) throw 2; std::printf("+"); } ~Elem() { std::printf("-"); } }; int Elem::made = 0;
int main() { try { new Boom; } catch (int) { std::printf("caught1 "); } try { new Elem[5]; } catch (int) { std::printf("caught2"); } std::printf("\n"); return 0; }
