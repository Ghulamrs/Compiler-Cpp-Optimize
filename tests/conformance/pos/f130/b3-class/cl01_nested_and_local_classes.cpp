#include <cstdio>
struct Outer { struct Inner; static int s; int v = 1; struct Inner { int w; int get(const Outer &o) const { return o.v + w + s; } }; }; int Outer::s = 10; struct Outer::Inner g_in{2};
int main() { struct Local { int x; int twice() const { return x * 2; } struct LL { int y; }; }; Local l{3}; Local::LL ll{4}; Outer o; std::printf("%d %d %d\n", g_in.get(o), l.twice(), ll.y); return 0; }
