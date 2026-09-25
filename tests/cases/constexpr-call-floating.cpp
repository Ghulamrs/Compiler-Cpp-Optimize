// A11: a constexpr call is folded in a floating constant expression, with
// floating parameters and a floating result, and an integer call widened.
extern "C" int printf(const char *, ...);
constexpr double half(double d) { return d / 2.0; }
constexpr int sq(int x) { return x * x; }
constexpr double pick(bool b, double x) { return b ? x : -x; }
constexpr double H = half(7.0);
constexpr double E = sq(3) / 2.0;
constexpr double N = pick(false, half(1.0));
constexpr int I = (int)half(9.0);
static_assert(I == 4, "cast of a folded call");
int main() { printf("%.2f %.2f %.2f %d\n", H, E, N, I); return 0; }
