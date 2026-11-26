#include <cstdio>
struct B { virtual const char *who() const { return "B"; } virtual ~B() {} }; struct D : B { const char *who() const override { return "D"; } };
int main() { try { throw D(); } catch (B &b) { std::printf("%s ", b.who()); } try { throw D(); } catch (B b) { std::printf("%s ", b.who()); } try { throw D(); } catch (const D &d) { std::printf("%s ", d.who()); } catch (...) { std::printf("x "); } std::printf("\n"); return 0; }
