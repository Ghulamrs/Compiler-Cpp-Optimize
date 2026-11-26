// [except.spec]/9 is only for what leaves: a noexcept function whose own handler ends the exception returns normally
#include <cstdio>
#include <exception>
struct L { ~L() { std::printf("~L "); } };
int risky(int x) { L l; if (x > 2) throw x; return x; }
int safe(int x) noexcept {
    try { return risky(x); } catch (int v) { return -v; }
}
int main() {
    std::printf("%d %d\n", safe(1), safe(5));
    return 0;
}
