// [except.unexpected]/2: a handler that throws what the list does not allow, and no std::bad_exception in it - std::terminate()
#include <cstdio>
#include <cstdlib>
#include <exception>
void f() throw(int) { throw 'c'; }
void onUnexpected() { std::printf("unexpected "); throw 2.5; }
void onTerminate() { std::printf("terminate\n"); std::fflush(stdout); std::exit(3); }
int main() {
    std::set_unexpected(onUnexpected);
    std::set_terminate(onTerminate);
    try { f(); } catch (...) { std::printf("caught\n"); }
    return 0;
}
