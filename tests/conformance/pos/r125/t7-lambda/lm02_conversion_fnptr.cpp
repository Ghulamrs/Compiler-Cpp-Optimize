// [expr.prim.lambda]/6: captureless lambdas convert to function pointers; passing to C-style callbacks; + trick; unary plus not needed; lambda type uniqueness; sizeof of closures.
#include <cstdio>
int apply(int (*f)(int), int v) { return f(v); }
typedef void (*Cb)(const char *);
void run(Cb cb) { cb("cb"); }
int main() {
    int (*fp)(int) = [](int x) { return x * 3; };
    std::printf("%d %d\n", fp(4), apply([](int x) { return x - 1; }, 4));
    run([](const char *s) { std::printf("%s\n", s); });
    auto l1 = [] {}; auto l2 = [] {};
    std::printf("%d\n", (int)sizeof(l1) == (int)sizeof(l2));
    int x = 0; auto l3 = [&x] { x++; }; l3(); l3();
    std::printf("%d %d\n", x, (int)sizeof(l3) == (int)sizeof(void *));
    auto plus = +[](int a) { return a + 1; };
    std::printf("%d\n", plus(1));
    double (*dp)(double) = [](double d) -> double { return d / 2; };
    std::printf("%g\n", dp(5));
    return 0;
}
