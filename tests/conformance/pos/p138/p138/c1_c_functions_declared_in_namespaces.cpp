// [dcl.link]/6: a function with C linkage declared in a namespace is one entity with every C declaration of
// its name, reached through the namespace and globally.
#include <cstdio>
namespace ns { extern "C" int cf(int x) { return x + 1; } }
extern "C" int cf(int);
extern "C" int g(int x) { return x * 2; }
namespace n { extern "C" int g(int); int call() { return g(5) + n::g(1); } }
int main() { std::printf("%d %d %d %d\n", ns::cf(1), cf(2), n::g(3), n::call()); return 0; }