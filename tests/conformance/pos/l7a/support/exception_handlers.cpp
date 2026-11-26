#include <cstdio>
#include <cstdlib>
#include <exception>
static void myTerminate() {
    std::printf("terminate handler, uncaught %d\n", (int)std::uncaught_exception());
    std::fflush(stdout);
    std::_Exit(0);
}
static void myUnexpected() {}
struct Watch {
    ~Watch() { std::printf("unwinding, uncaught %d\n", (int)std::uncaught_exception()); }
};
int main() {
    std::terminate_handler was = std::set_terminate(myTerminate);
    std::printf("%d %d\n", was != 0, std::get_terminate() == myTerminate);
    std::unexpected_handler u = std::set_unexpected(myUnexpected);
    std::printf("%d %d\n", u != myUnexpected, std::get_unexpected() == myUnexpected);
    std::printf("outside, uncaught %d\n", (int)std::uncaught_exception());
    try {
        Watch w;
        throw 1;
    } catch (int) {
        std::printf("caught, uncaught %d\n", (int)std::uncaught_exception());
    }
    std::bad_exception be;
    const std::exception &e = be;
    std::printf("%s\n", e.what());
    std::fflush(stdout);
    std::terminate();
}
