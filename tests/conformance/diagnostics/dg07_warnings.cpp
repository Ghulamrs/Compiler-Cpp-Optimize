// the warnings, and what must stay silent
#include <cstdio>
#include <cmath>
#include <limits>
void byRef(int &r) { r = 1; }
void byVal(int) {}
int main() {
    int y = 1;
    if (y = 2) std::printf("a\n");
    if ((y = 3)) std::printf("b\n");
    int u;
    std::printf("%d\n", u + 1);
    int v;
    byVal(v);
    int w;
    byRef(w);
    std::printf("%d\n", w);
    int z;
    z = 4;
    std::printf("%d\n", z);
    int p;
    int *pp = &p;
    *pp = 1;
    std::printf("%d\n", p);
    int t;
    int &tr = t;
    tr = 3;
    std::printf("%d\n", t);
    int c;
    auto set = [&] { c = 1; };
    set();
    std::printf("%d\n", c);
    double far = 1e400;
    float ff = 1e40f;
    double fine = HUGE_VAL;
    double inf = std::numeric_limits<double>::infinity();
    std::printf("%d\n", far == fine && inf == fine && ff > 1.0f);
    return 0;
}
