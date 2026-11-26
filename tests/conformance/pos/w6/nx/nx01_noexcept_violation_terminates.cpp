// [except.spec]/9, [except.terminate]/2: an exception leaving a noexcept function calls std::terminate()
#include <cstdio>
#include <cstdlib>
#include <exception>
void g(int x) { if (x) throw x; }
void f(int x) noexcept { g(x); }
void onTerminate() { std::printf("terminate\n"); std::fflush(stdout); std::exit(7); }
int main() {
    std::set_terminate(onTerminate);
    f(0);
    std::printf("quiet ");
    try { f(1); } catch (int) { std::printf("caught\n"); }
    return 0;
}
