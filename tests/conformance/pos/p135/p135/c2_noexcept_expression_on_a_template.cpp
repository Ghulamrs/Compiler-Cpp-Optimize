// R20: noexcept(e) on a function template's signature, folded with its arguments bound.
#include <cstdio>
struct Throws { Throws() {} };
struct Quiet { Quiet() noexcept {} };
template <class T> void make() noexcept(noexcept(T())) { T t; (void)t; }
template <class T> bool quiet() { return noexcept(make<T>()); }
int main() {
    make<Quiet>(); make<Throws>(); make<int>();
    std::printf("%d %d %d\n", (int)quiet<Quiet>(), (int)quiet<Throws>(), (int)quiet<int>());
    return 0;
}