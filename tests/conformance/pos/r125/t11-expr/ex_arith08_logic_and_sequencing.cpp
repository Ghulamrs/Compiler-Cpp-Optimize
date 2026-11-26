// Short-circuit evaluation, sequencing of && || ?: and comma, increments in sequenced contexts, compound assignment order, assignment as expression value, chained assignments with conversions.
#include <cstdio>
int calls = 0;
bool t() { ++calls; return true; }
bool f() { ++calls; return false; }
int main() {
    bool r1 = f() && t(); bool r2 = t() || f(); bool r3 = t() && f(); bool r4 = f() || t();
    std::printf("%d %d %d %d %d\n", r1, r2, r3, r4, calls);
    int i = 0; int j = (i++, i++, i); std::printf("%d %d\n", i, j);
    int k = 5; k += k; k *= 2; k -= 1; std::printf("%d\n", k);
    int a, b, c; a = b = c = 3; std::printf("%d %d %d\n", a, b, c);
    double dd; int ii; ii = dd = 2.75; std::printf("%d %g\n", ii, dd);
    int m = 1; if (m == 1 && (m = 2)) {} std::printf("%d\n", m);
    int p = 0; bool pb = (p = 5) && (p += 1); std::printf("%d %d\n", pb, p);
    int q = 3; q = q == 3 ? q * 2 : q; std::printf("%d\n", q);
    return 0;
}
