#include <cstdio>
#include <cstdlib>
static void not_run() { std::printf("atexit ran\n"); std::fflush(stdout); }
int main() {
    std::atexit(not_run);
    std::printf("leaving\n");
    std::fflush(stdout);
    std::_Exit(4);
}
