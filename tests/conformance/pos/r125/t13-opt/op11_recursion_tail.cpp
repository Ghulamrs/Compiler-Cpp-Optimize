// Deep-ish recursion (depth 100000 with small frames), mutual recursion, tail calls with destructors (must still run), recursion returning structs.
#include <cstdio>
struct D { static int n; ~D() { ++n; } };
int D::n = 0;
int down(int k) { return k == 0 ? 0 : 1 + down(k - 1); }
bool isEven(int n); bool isOdd(int n) { return n == 0 ? false : isEven(n - 1); } bool isEven(int n) { return n == 0 ? true : isOdd(n - 1); }
int withDtor(int k) { D d; if (k == 0) return 0; return withDtor(k - 1) + 1; }
struct Pair { int a, b; };
Pair fibPair(int n) { if (n == 0) { Pair p = {0, 1}; return p; } Pair q = fibPair(n - 1); Pair r = {q.b, q.a + q.b}; return r; }
int main() {
    std::printf("%d\n", down(100000));
    std::printf("%d %d\n", isEven(1001), isOdd(1001));
    std::printf("%d %d\n", withDtor(1000), D::n);
    std::printf("%d\n", fibPair(40).a);
    return 0;
}
