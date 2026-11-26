// Exception object lifetime: copied once on throw ([except.throw]/3 - a copy may be elided), destroyed after the handler; catch by value copies; throw; rethrows the same object; modifications in a handler persist through rethrow by reference.
#include <cstdio>
struct E { int v; static int live; E(int v) : v(v) { ++live; } E(const E &o) : v(o.v) { ++live; } ~E() { --live; } };
int E::live = 0;
void inner() { try { throw E(1); } catch (E &e) { e.v = 2; throw; } }
int main() {
    try { inner(); } catch (const E &e) { std::printf("%d %d\n", e.v, E::live > 0); }
    std::printf("%d\n", E::live);
    try { throw E(3); } catch (E e) { e.v = 4; std::printf("%d\n", e.v); }
    std::printf("%d\n", E::live);
    try { try { throw E(5); } catch (E &e) { throw e; } } catch (const E &e) { std::printf("%d\n", e.v); }
    std::printf("%d\n", E::live);
    return 0;
}
