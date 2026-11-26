#include <cstdio>
int foo = 10;
#define foo foo + 1
#define f(x) (x + 1)
#define g f
#define h() g
#define ID(x) x
#define AA BB
#define BB AA
int AA = 4, BB = 5;
int main() {
    std::printf("%d %d %d %d %d %d\n", foo, g(1), h()(2), ID(f)(3), AA, BB);
    return 0;
}
