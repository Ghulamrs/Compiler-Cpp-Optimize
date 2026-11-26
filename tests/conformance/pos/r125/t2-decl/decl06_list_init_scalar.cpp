// [dcl.init.list]: int x{}; int y{5}; empty braces value-initialise; T{} prvalue; double from int braces fine (not narrowing when constant fits); char c{'a'}.
#include <cstdio>
struct P { int a; double b; };
struct Q { Q() : v(7) {} int v; };
struct R { int x; int y; };
int take(R r) { return r.x * 10 + r.y; }
R make() { return {4, 5}; }
int main() {
    int a{}; int b{5}; double d{3}; char c{'z'}; bool f{}; unsigned u{7u}; long long ll{2147483648LL};
    std::printf("%d %d %g %c %d %u %lld\n", a, b, d, c, f, u, ll);
    int *p{}; std::printf("%d\n", p == nullptr);
    P q{1, 2.5}; P q2 = {3}; P q3{};
    std::printf("%d %g %d %g %d %g\n", q.a, q.b, q2.a, q2.b, q3.a, q3.b);
    Q w{}; std::printf("%d\n", w.v);
    std::printf("%d %d\n", take({1, 2}), take(R{3, 4}));
    R r = make(); std::printf("%d %d\n", r.x, r.y);
    int arr[]{1, 2, 3}; std::printf("%d %d\n", (int)(sizeof(arr) / sizeof(arr[0])), arr[2]);
    int z = int{}; std::printf("%d\n", z);
    double dd = double{2}; std::printf("%g\n", dd);
    std::printf("%d\n", int{3} + int{4});
    const int &ref{9}; std::printf("%d\n", ref);
    return 0;
}
