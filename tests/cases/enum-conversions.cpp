// What refusing int-to-enumeration must leave working: an enumerator into its
// own enumeration, a cast either way, an enumeration into an int, overloads
// that tell the two apart, promotion in arithmetic, a switch, and zeroing.
#include <stdio.h>

enum Colour { Red, Green, Blue };
enum Small : unsigned char { Lo = 1, Hi = 200 };

static int which(int)    { return 1; }
static int which(Colour) { return 2; }
static int wide(long)    { return 3; }
static int pick(int)     { return 4; }
static int pick(double)  { return 5; }

struct Pixel { Colour c; int n; };

static Colour next(Colour c) { return static_cast<Colour>((c + 1) % 3); }

static const char *name(Colour c) {
    switch (c) {
    case Red:   return "red";
    case Green: return "green";
    default:    return "blue";
    }
}

int main(void) {
    Colour c = Green;
    Colour d = Colour(2);
    Colour e = (Colour)0;
    int m = Blue;
    Small s = Hi;
    printf("%d %d %d %d %d\n", c, d, e, m, s);
    printf("%d %d %d\n", which(Red), which(Red + 1), which(m));
    printf("%d %d %d\n", wide(Blue), pick(Green), pick(s));
    printf("%s %s %s\n", name(c), name(next(c)), name(next(next(c))));
    Colour arr[3] = {};
    Pixel p = {};
    Pixel q = { Blue, 4 };
    printf("%d %d %d %d %d %d\n", arr[0], arr[2], p.c, p.n, q.c, q.n);
    c = d;
    printf("%d %d\n", c == Blue, c != Red);
    return 0;
}
