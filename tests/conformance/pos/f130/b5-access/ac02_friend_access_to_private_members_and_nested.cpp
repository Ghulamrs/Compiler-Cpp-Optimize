#include <cstdio>
class A { int x = 1; class In { int y = 2; friend class A; }; friend int look(const A &); public: int lookIn(const In &i) const { return i.y + x; } In mk() const { return In(); } }; int look(const A &a) { return a.x; }
int main() { A a; std::printf("%d %d\n", look(a), a.lookIn(a.mk())); return 0; }
