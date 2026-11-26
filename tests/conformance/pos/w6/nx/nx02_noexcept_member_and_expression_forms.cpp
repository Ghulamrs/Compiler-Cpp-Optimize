// [except.spec]/9: noexcept(true) on a member, and a handler inside the function that rethrows - the rethrow leaves it, std::terminate()
#include <cstdio>
#include <cstdlib>
#include <exception>
struct S {
    int n;
    void run(int x) noexcept(true) {
        try { if (x) throw x; } catch (int v) { n = v; std::printf("handled %d ", v); throw; }
    }
};
void onTerminate() { std::printf("terminate\n"); std::fflush(stdout); std::exit(4); }
int main() {
    std::set_terminate(onTerminate);
    S s = { 0 };
    s.run(0);
    try { s.run(6); } catch (...) { std::printf("caught\n"); }
    return 0;
}
