// [class.copy]/20: a class with no user-declared copy/move/dtor gets an implicit move assignment that moves each member.
#include <cstdio>
#include <utility>
struct Tr { Tr() {} Tr(const Tr &) { std::printf("c"); } Tr(Tr &&) { std::printf("m"); } Tr &operator=(const Tr &) { std::printf("C"); return *this; } Tr &operator=(Tr &&) { std::printf("M"); return *this; } };
struct Plain { Tr t; };
struct Two { Tr a; Tr b; };
struct Derived : Plain { Tr u; };
int main() {
    Plain p, q; q = std::move(p); std::printf(" plain\n");
    Two a, b; b = std::move(a); std::printf(" two\n");
    Derived d, e; e = std::move(d); std::printf(" derived\n");
    Plain r = std::move(q); std::printf(" ctor\n");
    return 0;
}
