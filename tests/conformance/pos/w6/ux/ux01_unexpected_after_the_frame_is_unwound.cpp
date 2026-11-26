// [except.unexpected]/1: an exception the list does not allow calls std::unexpected() once the function's own objects are destroyed
#include <cstdio>
#include <cstdlib>
#include <exception>
struct L { const char *n; ~L() { std::printf("~%s ", n); } };
void g(int x) { L inner = { "inner" }; if (x) throw 2.5; }
void f(int x) throw(int) { L local = { "local" }; g(x); std::printf("not here "); }
void onUnexpected() { std::printf("unexpected\n"); std::fflush(stdout); std::exit(5); }
int main() {
    std::set_unexpected(onUnexpected);
    try { f(1); } catch (double) { std::printf("caught double\n"); }
    return 0;
}
