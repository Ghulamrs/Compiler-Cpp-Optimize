// [class.copy]/9: implicit move ctor only when no user-declared copy ctor/assign/dtor; implicit move assign likewise; move of members that are move-only; defaulted move deleted if a member can't move -> falls back to copy? (no: deleted move is ignored by overload resolution only in C++11? it is NOT ignored - but defaulted-as-deleted move ctor IS ignored, [over.match.funcs]/8 came in C++14 via DR1402... use only clear cases).
#include <cstdio>
#include <utility>
struct Tr { Tr() {} Tr(const Tr &) { std::printf("c"); } Tr(Tr &&) { std::printf("m"); } Tr &operator=(const Tr &) { std::printf("C"); return *this; } Tr &operator=(Tr &&) { std::printf("M"); return *this; } };
struct Plain { Tr t; };
struct WithDtor { Tr t; ~WithDtor() {} };
struct WithCopyAssign { Tr t; WithCopyAssign() {} WithCopyAssign &operator=(const WithCopyAssign &o) { t = o.t; return *this; } };
struct WithMoveOnly { Tr t; WithMoveOnly() {} WithMoveOnly(WithMoveOnly &&) = default; };
struct Base { Tr t; };
struct Derived : Base { Tr u; };
int main() {
    Plain p; Plain p2 = std::move(p); p2 = std::move(p); std::printf(" plain\n");
    WithDtor d; WithDtor d2 = std::move(d); d2 = std::move(d); std::printf(" dtor\n");
    WithCopyAssign a; WithCopyAssign a2 = std::move(a); a2 = std::move(a); std::printf(" cassign\n");
    WithMoveOnly m; WithMoveOnly m2 = std::move(m); std::printf(" moveonly\n");
    Derived x; Derived y = std::move(x); y = std::move(x); std::printf(" derived\n");
    return 0;
}
