// **A scoped enumeration converts to nothing implicitly**, [dcl.enum]/10 with
// [conv.prom]/3: `f(int)` is not viable for a `Colour`, where an unscoped
// enumeration's argument would promote to it, and `f(long)` and `f(bool)` are
// not viable either - so the one overload taking the enumeration is chosen,
// and an int argument cannot reach it. Each overload returns its own number.
extern "C" { int printf(const char *, ...); }

enum class Colour : short { Red, Green };
enum Plain { One = 1 };

int f(int a)    { return 1 + 0 * a; }
int f(long a)   { return 2 + 0 * (int)a; }
int f(bool a)   { return 3 + 0 * (int)a; }
int f(Colour c) { return 4 + 0 * (int)c; }
int g(int a)    { return 5 + 0 * a; }
int g(Colour c) { return 6 + 0 * (int)c; }

int main(void) {
    int a = f(Colour::Green);
    int b = f(One);
    int c = g(Colour::Red);
    int d = g('x');
    printf("%d %d %d %d\n", a, b, c, d);
    return 0;
}
