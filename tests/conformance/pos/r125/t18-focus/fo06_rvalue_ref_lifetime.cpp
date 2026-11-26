// [class.temporary]/5: a temporary bound to T&& lives as long as the reference. Writes through the reference after the full-expression must reach a live object.
#include <cstdio>
struct T { int v; T(int v) : v(v) { std::printf("+%d ", v); } ~T() { std::printf("-%d ", v); } };
T make(int v) { return T(v); }
int main() {
    { T &&r = make(1); r.v = 11; std::printf("[%d] ", r.v); } std::printf("| rvalue-ref\n");
    { const T &c = make(2); std::printf("[%d] ", c.v); } std::printf("| const-ref\n");
    { auto &&a = make(3); a.v = 33; std::printf("[%d] ", a.v); } std::printf("| auto&&\n");
    return 0;
}
