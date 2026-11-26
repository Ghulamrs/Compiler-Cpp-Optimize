// [basic.lookup.qual]/4: `::name` is looked up in the global namespace alone.
#include <cstdio>
int g = 3;
int f() { return 10; }
int h(int x) { return x + 100; }
enum Colour { Red = 7 };
namespace N { int x = 20; }
namespace n {
    int g = 4;
    int f() { return 40; }
    int both() { return ::g + g + ::f() + f(); }
}
struct S {
    int g = 5;
    static int f() { return 50; }
    int mine() const { return ::g + g + ::f() + f(); }
};
int main() {
    int g = 6;
    int (*p)(int) = ::h;
    std::printf("%d %d %d %d %d %d %d\n", ::g + g, n::both(), S().mine(), p(1), ::Red, ::N::x, ::h(2));
    return 0;
}