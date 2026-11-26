#include <cstdio>
struct E { int k; E(int x) : k(x) {} E(E &&o) : k(o.k) { std::printf("mv "); } E(const E &o) : k(o.k) { std::printf("cp "); } };
void p(E &e) { throw e; }
void q(E e) { try { throw e; } catch (int) {} }
int main() { E a(1); try { p(a); } catch (E &) {} try { E b(2); try { throw b; } catch (E &c) { throw c; } } catch (E &) {} std::printf("\n"); return 0; }
