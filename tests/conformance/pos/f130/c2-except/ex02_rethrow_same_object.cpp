#include <cstdio>
struct E { int v; E(int v) : v(v) {} E(const E &o) : v(o.v + 100) {} };
int main() { try { try { throw E(1); } catch (E &e) { e.v = 2; throw; } } catch (E &e) { std::printf("%d\n", e.v); } try { try { throw 5; } catch (int) { throw; } } catch (int i) { std::printf("%d\n", i); } return 0; }
