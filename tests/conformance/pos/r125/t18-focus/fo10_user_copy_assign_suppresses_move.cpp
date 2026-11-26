// [class.copy]/9: a user-declared copy assignment operator suppresses the implicit move constructor; Tr's copy must run, not its move.
#include <cstdio>
#include <utility>
struct Tr { Tr() {} Tr(const Tr &) { std::printf("c"); } Tr(Tr &&) { std::printf("m"); } Tr &operator=(const Tr &) { std::printf("C"); return *this; } Tr &operator=(Tr &&) { std::printf("M"); return *this; } };
struct UserAssign { Tr t; UserAssign() {} UserAssign &operator=(const UserAssign &o) { t = o.t; return *this; } };
struct UserCopy { Tr t; UserCopy() {} UserCopy(const UserCopy &o) : t(o.t) {} };
struct UserDtor { Tr t; ~UserDtor() {} };
int main() {
    UserAssign a; UserAssign b = std::move(a); std::printf(" user-assign\n");
    UserCopy c; UserCopy d = std::move(c); std::printf(" user-copy\n");
    UserDtor e; UserDtor f = std::move(e); f = std::move(e); std::printf(" user-dtor\n");
    return 0;
}
