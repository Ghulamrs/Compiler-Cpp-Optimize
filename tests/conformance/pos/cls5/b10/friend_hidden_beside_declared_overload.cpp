// [namespace.memdef]/3: `twice(int)` declared only by a friend declaration is not a candidate for
// `twice(3)`, whose int argument has no class; the declared `twice(long)` is the one called.
#include <cstdio>

class A {
    friend int twice(int);
    int x;
};

int twice(long v) { return static_cast<int>(v) * 2 + 1; }

int main() {
    std::printf("%d\n", twice(3));
    return 0;
}
