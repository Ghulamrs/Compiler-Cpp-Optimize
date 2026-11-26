// Lifetime extension: a temporary bound to T&& or const T& lives as long as the reference ([class.temporary]/5); not extended through a function return; member of a temporary bound to a reference extends the whole temporary; std::move does not extend anything.
#include <cstdio>
#include <utility>
struct T { int v; T(int v) : v(v) { std::printf("+%d ", v); } ~T() { std::printf("-%d ", v); } };
struct Pair { T a; T b; Pair() : a(1), b(2) {} };
T make(int v) { return T(v); }
const T &pass(const T &t) { return t; }
int main() {
    { T &&r = make(10); r.v = 11; std::printf("[%d] ", r.v); } std::printf("| rref\n");
    { const T &r = make(20); std::printf("[%d] ", r.v); } std::printf("| cref\n");
    { const T &m = Pair().b; std::printf("[%d] ", m.v); } std::printf("| member\n");
    { const T &p = pass(make(30)); std::printf("| after-call "); (void)p; } std::printf("| through-fn\n");
    { T t(40); T &&r = std::move(t); r.v = 41; std::printf("[%d] ", t.v); } std::printf("| move-ref\n");
    return 0;
}
