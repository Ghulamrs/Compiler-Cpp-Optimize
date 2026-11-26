// [except.spec]: noexcept operator values for various expressions; noexcept(expr) specs; throw() as noexcept; noexcept on function pointers is not part of the type in C++11; destructors implicitly noexcept; noexcept violation terminates (checked in a separate program).
#include <cstdio>
#include <utility>
void may() {}
void no() noexcept {}
void old() throw() {}
void cond(int) noexcept(sizeof(int) == 4) {}
struct S { S() {} ~S() {} void m() noexcept {} void n() {} int v; };
struct Throwing { ~Throwing() noexcept(false) {} };
struct Mv { Mv() {} Mv(Mv &&) noexcept {} Mv(const Mv &) {} };
template <class T> bool moveIsNoexcept() { return noexcept(T(std::declval<T>())); }
int main() {
    S s; int i = 0;
    std::printf("%d %d %d %d\n", noexcept(may()), noexcept(no()), noexcept(old()), noexcept(cond(1)));
    std::printf("%d %d %d %d\n", noexcept(s.m()), noexcept(s.n()), noexcept(i + 1), 0);
    std::printf("%d %d %d\n", noexcept(S()), noexcept(s.~S()), noexcept(Throwing()));
    std::printf("%d %d\n", noexcept(new int), noexcept(i = 2));
    void (*fp)() = no; std::printf("%d\n", noexcept(fp()) == 0);
    no(); old(); cond(0);
    return 0;
}
