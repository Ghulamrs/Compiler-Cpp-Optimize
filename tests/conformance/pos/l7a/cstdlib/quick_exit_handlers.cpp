#include <cstdio>
#include <cstdlib>
static void not_run() { std::printf("atexit ran\n"); std::fflush(stdout); }
static void one() { std::printf("one\n"); std::fflush(stdout); }
static void two() { std::printf("two\n"); std::fflush(stdout); }
int main() {
    std::atexit(not_run);
    std::at_quick_exit(one);
    std::at_quick_exit(two);
    std::printf("leaving\n");
    std::fflush(stdout);
    std::quick_exit(3);
}
