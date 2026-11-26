// [stmt.ranged] inside a handler: the range and its iterators have destructors
#include <cstdio>
struct It { int *p; It(int *p) : p(p) {} ~It() { std::printf("~It "); } int &operator*() { return *p; } It &operator++() { ++p; return *this; } bool operator!=(const It &o) const { return p != o.p; } };
struct R { int a[3]; R() { a[0] = 1; a[1] = 2; a[2] = 3; } ~R() { std::printf("~R "); } It begin() { return It(a); } It end() { return It(a + 3); } };
int main() {
    try { throw 1; }
    catch (int) { int s = 0; for (int v : R()) s += v; std::printf("s%d\n", s); }
    return 0;
}
