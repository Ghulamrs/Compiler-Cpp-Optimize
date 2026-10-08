// A lambda that captures nothing converts to a pointer to function of its
// signature - [expr.prim.lambda]/6 - and the pointer calls the body.
extern "C" int printf(const char *, ...);

static int apply(int (*f)(int), int x) { return f(x); }
static double twice(double (*g)(double, double), double a) { return g(a, a); }

struct Big { int a, b, c, d, e; };
static int sumBig(int (*f)(Big), Big v) { return f(v); }

int main() {
    int (*sq)(int) = [](int x) { return x * x; };
    printf("assigned %d\n", sq(7));
    int r = apply([](int x) { return x + 100; }, 5);
    printf("argument %d\n", r);
    double d = twice([](double a, double b) { return a * b + 0.5; }, 3.0);
    printf("two parameters %g\n", d);
    void (*say)() = [] { printf("void lambda\n"); };
    say();
    int (*byRef)(const int &) = [](const int &v) { return v - 1; };
    printf("reference %d\n", byRef(10));
    Big b = { 1, 2, 3, 4, 5 };
    int s = sumBig([](Big v) { return v.a + v.b + v.c + v.d + v.e; }, b);
    printf("by value %d\n", s);
    auto f = [](int x) { return x * 3; };
    int (*fp)(int) = f;
    int (*fp2)(int) = static_cast<int (*)(int)>(f);
    int u = fp(2);
    int w = fp2(4);
    printf("named %d %d\n", u, w);
    return 0;
}
