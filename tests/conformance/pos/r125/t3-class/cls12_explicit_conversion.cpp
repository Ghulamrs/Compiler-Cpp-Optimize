// [class.conv.fct]/2 explicit conversion operators: contextual bool conversion in if/while/!/&&/?:, static_cast, not implicit; explicit ctors and copy-list-init refusal checked in neg tests.
#include <cstdio>
struct Flag { int v; explicit operator bool() const { return v != 0; } explicit operator int() const { return v * 2; } };
struct Impl { int v; operator int() const { return v; } };
struct Ex { explicit Ex(int v) : v(v) {} int v; };
int takeEx(const Ex &e) { return e.v; }
int main() {
    Flag f = {3}, z = {0};
    if (f) std::printf("if "); if (!z) std::printf("not "); if (f && !z) std::printf("and "); std::printf("%d ", f ? 1 : 0); while (z) {} std::printf("%d\n", static_cast<int>(f));
    bool b = static_cast<bool>(f); bool c(f); std::printf("%d %d\n", b, c);
    Impl i = {4}; int n = i + 1; std::printf("%d\n", n);
    std::printf("%d %d\n", takeEx(Ex(5)), takeEx(Ex{6}));
    Ex e{7}; std::printf("%d\n", e.v);
    return 0;
}
