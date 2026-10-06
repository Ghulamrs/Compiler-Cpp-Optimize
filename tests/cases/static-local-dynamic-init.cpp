// [stmt.dcl]/4: a static local whose initialiser is not a constant expression is
// initialised the first time control passes through its declaration, under the same
// guard a constructor runs under, and never again. A constant one stays data.
extern "C" int printf(const char *, ...);

static int made = 0;
static int start(int v) { made++; return v; }

static int counted(int k) { static int calls = k * 10; return ++calls; }
static double scaled(double x) { static double first = x * 1.5; return first; }
static const char *named(const char *name) { static const char *p = name; return p; }

static int g = 40;
static int &global() { made++; return g; }
static int viaRef() { static int &r = global(); return ++r; }

// Recursion: the initialiser runs at the outermost entry and the inner calls find it done.
static int depth(int n) {
    static int entered = start(100);
    entered++;
    return n == 0 ? entered : depth(n - 1);
}

// A const one with a value only known at run time is written once, so it is not in .rodata.
static int fixedAt(int k) { static const int at = start(k) + 1; return at; }

// A class with no constructor, from a call and from another static: its bytes, once each.
struct P { int x, y; };
static P make(int v) { made++; P p; p.x = v; p.y = v + 1; return p; }
static int pair(int v) { static P p = make(v); static P q = p; return p.y * 100 + q.x; }

// A constant initialiser needs no guard and no call.
static int constant() { static int c = 6 * 7; return c++; }

// Each call is a statement of its own: the order arguments are evaluated in is unspecified.
int main() {
    int a = counted(3), b = counted(4), c = counted(5);
    printf("%d %d %d\n", a, b, c);
    double x = scaled(2.0), y = scaled(9.0);
    printf("%.2f %.2f\n", x, y);
    const char *p = named("first"), *q = named("second");
    printf("%s %s\n", p, q);
    int r1 = viaRef(), r2 = viaRef();
    printf("%d %d %d\n", r1, r2, g);
    int d1 = depth(3), d2 = depth(2);
    printf("%d %d\n", d1, d2);
    int f1 = fixedAt(7), f2 = fixedAt(9);
    printf("%d %d\n", f1, f2);
    int s1 = pair(3), s2 = pair(8);
    printf("%d %d\n", s1, s2);
    int k1 = constant(), k2 = constant();
    printf("%d %d\n", k1, k2);
    printf("made %d\n", made);
    return 0;
}
