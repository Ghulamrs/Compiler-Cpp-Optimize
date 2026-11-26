// [over.ics.user]: one user-defined conversion per sequence; constructor vs conversion function; ambiguity avoided; conversion then standard conversion; explicit not considered for copy-init.
#include <cstdio>
struct From { int v; };
struct To { int v; To(const From &f) : v(f.v * 2) {} To(int i) : v(i) {} };
struct Conv { int v; operator int() const { return v; } operator double() const { return v + 0.5; } };
struct Lng { operator long() const { return 7; } };
const char *take(To t) { static char b[16]; std::snprintf(b, 16, "To%d", t.v); return b; }
const char *num(int) { return "int"; }
const char *num(double) { return "double"; }
const char *lng(long) { return "long"; }
const char *lng(int) { return "int"; }
int main() {
    From f = {3};
    std::printf("%s ", take(f)); std::printf("%s\n", take(5));
    Conv c = {4}; int i = c; double d = c; std::printf("%d %g\n", i, d);
    std::printf("%s %s\n", num(static_cast<int>(c)), num(static_cast<double>(c)));
    Lng l; std::printf("%s\n", lng(l));
    short s = Lng(); std::printf("%d\n", s);
    To t = f; std::printf("%d\n", t.v);
    To t2 = 9; std::printf("%d\n", t2.v);
    return 0;
}
