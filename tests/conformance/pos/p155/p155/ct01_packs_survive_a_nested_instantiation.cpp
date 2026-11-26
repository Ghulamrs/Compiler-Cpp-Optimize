#include <cstdio>
#include <memory>
#include <utility>

// make_shared has packs named Args and args of its own; instantiating it inside a function template with the
// same names must leave that template's packs as they were.
template <class F, class... Args> int run(F &&f, Args &&... args) {
    std::shared_ptr<int> one = std::make_shared<int>(1);
    return f(std::forward<Args>(args)...) + *one;
}
static int add(int a, int b, int c) { return a + b + c; }
int main() {
    std::printf("%d %d\n", run(add, 1, 2, 3), run([] { return 10; }));
    return 0;
}
