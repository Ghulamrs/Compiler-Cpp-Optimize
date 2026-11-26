#include <cstdio>
#define debug(...) std::printf(__VA_ARGS__)
#define showlist(...) std::puts(#__VA_ARGS__)
#define report(test, ...) ((test)?std::puts(#test):\
    std::printf(__VA_ARGS__))
int main() {
    int x = 1, y = 2;
    debug("Flag\n");
    debug("X = %d\n", x);
    showlist(The first, second, and third items.);
    report(x>y, "x is %d but y is %d\n", x, y);
    report(x<y, "unused");
    return 0;
}
