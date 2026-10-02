// **An enumeration is its own type to overloading, and promotes to int**,
// [conv.prom]/3 and [over.ics.rank]: an enumerator matches f(Colour) exactly,
// reaches f(int) by promotion and so beats f(long) and f(double), and an int
// reaches f(Colour) not at all. A based enumeration promotes through its base.
extern "C" { int printf(const char *, ...); }

enum Colour { Red, Green };
enum Byte : unsigned char { Lo, Hi };

int f(int a)    { return 1 + 0 * a; }
int f(Colour a) { return 2 + 0 * (int)a; }
int g(long a)   { return 3 + 0 * (int)a; }
int g(int a)    { return 4 + 0 * a; }
int h(double a) { return 5 + 0 * (int)a; }
int h(int a)    { return 6 + 0 * a; }
int k(Colour a) { return 7 + 0 * (int)a; }
int k(long a)   { return 8 + 0 * (int)a; }

int main(void) {
    int n = 1;
    Colour c = Green;
    printf("%d %d %d %d\n", f(Red), f(c), f(n), f(Red + 1));
    printf("%d %d %d %d\n", g(Green), h(Green), g(Hi), h(Lo));
    printf("%d %d\n", k(Red), k(n));
    return 0;
}
