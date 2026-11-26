// a local filled through a non-const reference by a call is assigned from there on, and reading it says nothing - in
// the if the call is the condition of, after it, and in a later call; one passed by value is read before anything is
// assigned to it, and warned of
#include <cstdio>
struct Range { static bool special(double x, double &result); };
bool Range::special(double x, double &result) { result = x * 2; return x < 0; }
bool fill(int &r) { r = 5; return true; }
int sum(int a, int b) { return a + b; }
int look(const int &r) { return r; }
double outOfIf(double x) {
    double result;
    if (Range::special(x, result)) return result;
    return result + 1;
}
int main() {
    int a;
    if (fill(a)) std::printf("%d\n", a);
    int b;
    while (!fill(b)) {}
    std::printf("%d\n", b);
    int c, d;
    if (fill(c) && fill(d)) std::printf("%d\n", sum(c, d));
    int e;
    std::printf("%d\n", sum(e, 1));
    int f;
    if (look(f)) std::printf("f\n");
    std::printf("%g\n", outOfIf(-1.0));
    return 0;
}
