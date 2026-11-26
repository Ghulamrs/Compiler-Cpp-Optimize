// Exceptions crossing template instantiations, lambdas, function pointers, member functions, virtual calls, and recursion; catching a template class exception by template base.
#include <cstdio>
template <class T> struct Ex { T payload; };
template <class T> void thrower(T v) { throw Ex<T>{v}; }
struct I { virtual void f() = 0; virtual ~I() {} };
struct Impl : I { void f() override { throw 42; } };
int viaPtr(void (*fp)()) { try { fp(); } catch (int v) { return v; } return -1; }
void fpThrow() { throw 8; }
int recurse(int n) { if (n == 0) throw n; return recurse(n - 1); }
int main() {
    try { thrower(2.5); } catch (const Ex<double> &e) { std::printf("%g\n", e.payload); }
    try { thrower(7); } catch (const Ex<int> &e) { std::printf("%d\n", e.payload); }
    auto lam = [](int k) { if (k) throw k * 2; return 0; };
    try { lam(5); } catch (int v) { std::printf("%d\n", v); }
    Impl im; I &ref = im; try { ref.f(); } catch (int v) { std::printf("%d\n", v); }
    std::printf("%d\n", viaPtr(fpThrow));
    try { recurse(50); } catch (int v) { std::printf("%d\n", v); }
    try { [] { throw 3; }(); } catch (int v) { std::printf("%d\n", v); }
    return 0;
}
