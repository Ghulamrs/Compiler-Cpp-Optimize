// [except.spec]/9: std::terminate, the installed handler - not abort.
#include <cstdio>
#include <cstdlib>
#include <exception>
void direct() noexcept { throw 2; }
static void bye() { std::printf("terminated\n"); std::fflush(stdout); std::_Exit(0); }
int main() {
    std::set_terminate(bye);
    try { direct(); } catch (...) { std::printf("caught\n"); }
    std::printf("not here\n");
    return 3;
}
