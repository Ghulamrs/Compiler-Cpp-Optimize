// [except.spec]/14: a destructor with no specification has the implicit one's - non-throwing unless a base or a
// member says otherwise, so what escapes it ends the program after one run.
#include <cstdio>
#include <cstdlib>
#include <exception>
struct Plain { ~Plain() {} };
struct Open { ~Open() noexcept(false) {} };
struct HasOpen { Open o; ~HasOpen() {} };
struct FromOpen : Open {};
void thrower() { throw 1; }
struct Ends { ~Ends() { std::printf("~Ends\n"); thrower(); } };
static void bye() { std::printf("terminated\n"); std::fflush(stdout); std::_Exit(0); }
int main() {
    const int a = noexcept(Plain());
    const int b = noexcept(Open());
    const int c = noexcept(HasOpen());
    const int d = noexcept(FromOpen());
    std::printf("%d %d %d %d\n", a, b, c, d);
    std::set_terminate(bye);
    try { Ends e; } catch (...) { std::printf("caught\n"); }
    std::printf("not here\n");
    return 3;
}
