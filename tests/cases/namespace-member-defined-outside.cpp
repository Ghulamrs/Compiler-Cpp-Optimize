// Declared in the namespace, defined outside it by qualified name - the shape
// of every library header (the cl review's A17).
extern "C" int printf(const char *, ...);
namespace ns { int f(int); int g(double); namespace in { int h(); } }
int ns::f(int x) { return x + 1; }
int ns::g(double d) { return (int)(d * 2); }
int ns::in::h() { return 9; }
int main() { printf("%d %d %d\n", ns::f(1), ns::g(2.5), ns::in::h()); return 0; }
