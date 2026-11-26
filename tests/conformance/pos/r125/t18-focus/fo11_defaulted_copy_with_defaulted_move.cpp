// A class that defaults both its copy and its move constructor has both; copying from an lvalue uses the copy constructor.
#include <cstdio>
#include <utility>
struct Tr { Tr() {} Tr(const Tr &) { std::printf("c"); } Tr(Tr &&) { std::printf("m"); } };
struct Both { Tr t; Both() = default; Both(Both &&) = default; Both(const Both &) = default; };
struct MoveOnlyDefaulted { Tr t; MoveOnlyDefaulted() = default; MoveOnlyDefaulted(MoveOnlyDefaulted &&) = default; };
int main() {
    Both a; Both b = std::move(a); Both c = b; std::printf(" both\n");
    MoveOnlyDefaulted m; MoveOnlyDefaulted n = std::move(m); std::printf(" move-only\n");
    return 0;
}
