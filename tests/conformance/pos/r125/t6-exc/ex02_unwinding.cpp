// [except.ctor]: destructors run during unwinding in reverse order; partially constructed objects (members built before the throw are destroyed, the object itself is not); arrays partially constructed; exceptions from constructors with new.
#include <cstdio>
struct T { const char *n; T(const char *n) : n(n) { std::printf("+%s ", n); } ~T() { std::printf("-%s ", n); } };
struct Thrower { T a; T b; Thrower() : a("a"), b("b") { throw 1; } ~Thrower() { std::printf("~Thrower "); } };
struct Member { Thrower t; T after; Member() : after("after") {} };
struct ArrElem { static int made; int id; ArrElem() : id(++made) { std::printf("+e%d ", id); if (made == 3) throw 2; } ~ArrElem() { std::printf("-e%d ", id); } };
int ArrElem::made = 0;
void deep(int n) { T t(n == 0 ? "d0" : n == 1 ? "d1" : "d2"); if (n == 0) throw 9; deep(n - 1); }
int main() {
    try { T outer("outer"); Thrower th; std::printf("unreached "); } catch (int e) { std::printf("| caught %d\n", e); }
    try { Member m; } catch (int e) { std::printf("| caught %d\n", e); }
    try { ArrElem arr[5]; std::printf("unreached "); } catch (int e) { std::printf("| caught %d\n", e); }
    try { deep(2); } catch (int e) { std::printf("| caught %d\n", e); }
    try { T *p = new T("heap"); try { throw 3; } catch (...) { delete p; throw; } } catch (int e) { std::printf("| caught %d\n", e); }
    return 0;
}
