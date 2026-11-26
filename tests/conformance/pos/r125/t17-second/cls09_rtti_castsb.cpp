// [expr.dynamic.cast], [expr.typeid]: dynamic_cast across a hierarchy, to void*, failure as null and as bad_cast; typeid names compared; static_cast down; const_cast; reinterpret_cast round trip.
#include <cstdio>
#include <typeinfo>
#include <cstring>
struct A { virtual ~A() {} int a; };
struct B : A { int b; };
struct C : A { int c; };
struct M : B { int m; };
int main() {
    M m; A *ab = static_cast<B *>(&m);
    B *bb = dynamic_cast<B *>(ab); C *cc = dynamic_cast<C *>(ab); M *mm = dynamic_cast<M *>(ab);
    std::printf("%d %d %d\n", bb != 0, cc == 0, mm == &m);
    B b; A *pb = &b; std::printf("%d %d\n", dynamic_cast<C *>(pb) == 0, dynamic_cast<M *>(pb) == 0);
    void *vp = dynamic_cast<void *>(ab); std::printf("%d\n", vp == (void *)&m);
    try { C &rc = dynamic_cast<C &>(*pb); (void)rc; std::printf("no throw\n"); } catch (...) { std::printf("bad_cast\n"); }
    std::printf("%d %d %d\n", typeid(*ab) == typeid(M), typeid(*pb) == typeid(B), typeid(ab) == typeid(A *));
    std::printf("%d %d\n", typeid(int) == typeid(int), typeid(int) == typeid(unsigned));
    std::printf("%d\n", std::strcmp(typeid(int).name(), typeid(long).name()) != 0);
    const int ci = 3; int *pi = const_cast<int *>(&ci); std::printf("%d\n", pi == &ci);
    long long l = 0x1234; void *p = reinterpret_cast<void *>(l); std::printf("%d\n", reinterpret_cast<long long>(p) == l);
    std::printf("%d\n", typeid(M).before(typeid(A)) != typeid(A).before(typeid(M)));
    return 0;
}
