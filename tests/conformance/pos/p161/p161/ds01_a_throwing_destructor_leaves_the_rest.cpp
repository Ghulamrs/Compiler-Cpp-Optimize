// [except.ctor]/2: an object whose destruction ends in an exception still has its other subobjects destroyed - after
// a written body that throws, after a member's or a base's destructor that throws, implicit destructor or written.
#include <cstdio>
struct Q { int id; explicit Q(int i) : id(i) {} ~Q() { std::printf("~Q%d ", id); } };
struct B { ~B() { std::printf("~B "); } };
struct Thrower { ~Thrower() noexcept(false) { std::printf("~Thrower "); throw 2; } };
struct T1 { Q a; Q b; T1() : a(3), b(4) {} ~T1() noexcept(false) { std::printf("~T1 "); throw 1; } };
struct T3 { Q a; Thrower t; Q b; T3() : a(6), b(7) {} };
struct T4 : B, Thrower { Q a; T4() : a(8) {} };
struct T5 { Q a; Thrower t; Q b; T5() : a(9), b(10) {} ~T5() noexcept(false) { std::printf("~T5 "); } };
struct Count {
    static int n;
    int id;
    Count() : id(n++) {}
    ~Count() noexcept(false) { std::printf("~C%d ", id); if (id == 1) throw 7; }
};
int Count::n = 0;
int main() {
    try { T1 t; } catch (int e) { std::printf("caught%d\n", e); }
    try { T3 t; } catch (int e) { std::printf("caught%d\n", e); }
    try { T4 t; } catch (int e) { std::printf("caught%d\n", e); }
    try { T5 t; } catch (int e) { std::printf("caught%d\n", e); }
    try { Count arr[3]; } catch (int e) { std::printf("caught%d\n", e); }
    Count::n = 0;
    try { Count *p = new Count[3]; delete[] p; } catch (int e) { std::printf("caught%d\n", e); }
    return 0;
}
