#include <cstdio>
#include <cstring>
namespace N { int f(int) { return 1; } }
namespace M { int f(const char *) { return 2; } }
using N::f;
using M::f;
namespace N { int f(double) { return 3; } }
int g(int a, int b, int c = 3);
int g(int a, int b = 2, int c);
int g(int a, int b, int c) { return a * 100 + b * 10 + c; }
typedef int (*FP)(int);
int twice(int x) { return x * 2; }
struct Conv { operator FP() const { return twice; } };
struct Holder : Conv {};
int main() {
    Conv c; Holder h;
    std::size_t (*len)(const char *) = std::strlen;
    std::printf("%d %d %d %d %d %d %d\n", f(1), f("x"), f(2.5), g(4), g(4, 5), c(3) + h(4), (int)len("four"));
    return 0;
}
