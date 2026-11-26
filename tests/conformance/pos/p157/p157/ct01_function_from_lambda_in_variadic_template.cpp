#include <cstdio>
#include <functional>
#include <utility>

// A std::function made from a lambda inside a variadic function template, then a call expanding the pack: the
// flag its constructor template's new-expression keeps belongs to that constructor's frame, not the caller's.
static int add(int a, int b, int c) { return a + b + c; }
template <class F, class... Args> int run(F &&f, Args &&... args) {
    int calls = 0;
    std::function<void()> count = [&calls] { ++calls; };
    count();
    count();
    return f(std::forward<Args>(args)...) * 10 + calls;
}
int main() {
    std::printf("%d %d\n", run(add, 1, 2, 3), run([](int x) { return x; }, 4));
    return 0;
}
