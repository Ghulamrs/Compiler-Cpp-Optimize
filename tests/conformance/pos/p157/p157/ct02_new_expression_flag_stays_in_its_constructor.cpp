#include <cstdio>
#include <functional>
#include <memory>
#include <utility>

// A constructor template whose body holds a new-expression, made while the caller's statement is read: the
// flag that frees the memory if the constructor throws is zeroed and tested in the constructor's own frame.
struct Holder {
    int *p;
    template <class T> explicit Holder(T v) : p(new int(static_cast<int>(v))) {}
    ~Holder() { delete p; }
};
static int add(int a, int b, int c) { return a + b + c; }
template <class F, class... Args> int run(F &&f, Args &&... args) {
    std::shared_ptr<int> one = std::make_shared<int>(1);
    Holder h(2.5);
    std::function<void()> nothing = [] {};
    nothing();
    return f(std::forward<Args>(args)...) + *one + *h.p;
}
int main() {
    std::printf("%d\n", run(add, 1, 2, 3));
    return 0;
}
