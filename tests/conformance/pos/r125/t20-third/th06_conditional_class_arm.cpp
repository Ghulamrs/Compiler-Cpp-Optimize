// [expr.cond]/3: a class arm converts to the other arm's type through its conversion function.
#include <cstdio>
struct F { double v; operator double() const { return v; } };
struct N { int v; operator int() const { return v; } };
int main() { F f = {0.5}; N n = {3}; bool t = true; double d = t ? f : 1.0; int i = t ? n : 7; double e = !t ? f : 2.0; std::printf("%g %d %g\n", d, i, e); return 0; }
