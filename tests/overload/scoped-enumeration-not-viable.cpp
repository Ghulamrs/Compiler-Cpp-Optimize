// **Nothing reaches a scoped enumeration's integer but a cast**: with only
// `f(int)` and `f(double)` declared, `f(Colour::Red)` has no viable function at
// all, where an unscoped enumerator would promote to int. clang refuses it.
extern "C" { int printf(const char *, ...); }

enum class Colour { Red, Green };

int f(int a)    { return 1 + 0 * a; }
int f(double a) { return 2 + 0 * (int)a; }

int main(void) {
    printf("%d\n", f(Colour::Red));
    return 0;
}
