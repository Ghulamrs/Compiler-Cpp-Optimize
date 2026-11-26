// [ios.base]: the bitmasks of ios_base, named through its derived classes and their typedefs.
#include <cstdio>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <typeinfo>
template <class S> int openWith(S &s) { s.open("no such file here", S::in | S::binary); return s.is_open() ? 1 : 0; }
void take(std::ios::openmode m = std::ios::in | std::ios::binary) { std::printf("%d\n", (int)(m != 0)); }
struct Holder {
    std::ifstream in;
    Holder() : in("no such file here", std::ifstream::in | std::ifstream::binary) {}
};
int f1(int x) { return x + 1; }
struct Func { int operator()(int x) const { return x * 2; } };
int main() {
    std::stringstream c(std::stringstream::in | std::stringstream::binary);
    std::ifstream d;
    std::printf("%d\n", openWith(d));
    take();
    Holder h;
    std::printf("%d\n", h.in.is_open() ? 1 : 0);
    const std::ios::openmode cm = std::ios_base::app;
    switch (std::ios::end) {
    case std::ios::end: std::printf("end\n"); break;
    default: break;
    }
    std::function<int(int)> fn = Func();
    const std::function<int(int)> cf = f1;
    std::printf("%d\n", fn.target<Func>() != 0 ? 1 : 0);
    std::printf("%d\n", cf.target<int (*)(int)>() != 0 ? 1 : 0);
    std::printf("%d\n", cm != 0 ? 1 : 0);
    std::printf("%d\n", fn.target_type() == typeid(Func) ? 1 : 0);
    return 0;
}
