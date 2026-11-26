// [dcl.dcl]/4: static_assert at namespace, class, block scope and in templates; passes silently.
#include <cstdio>
static_assert(sizeof(int) == 4, "int is 4");
static_assert(sizeof(long long) == 8, "ll is 8");
template <class T> struct Box { static_assert(sizeof(T) <= 8, "small"); T t; };
struct C { static_assert(1 + 1 == 2, "math"); int x; };
constexpr int sq(int x) { return x * x; }
int main() {
    static_assert(sq(4) == 16, "constexpr in static_assert");
    static_assert(true, "");
    Box<int> b = {3}; C c = {4};
    std::printf("%d %d\n", b.t, c.x);
    return 0;
}
