// auto& deduced from a const object is a reference to const: a write through it must be refused; here the deduced type is tested without writing.
#include <cstdio>
template <class A, class B> struct Same { static const bool v = false; };
template <class A> struct Same<A, A> { static const bool v = true; };
const int gc = 5;
const int &cref() { return gc; }
int main() {
    auto &a = gc; auto &b = cref(); const auto &c = gc;
    std::printf("%d %d %d\n", Same<decltype(a), const int &>::v, Same<decltype(b), const int &>::v, Same<decltype(c), const int &>::v);
    auto *p = &gc; std::printf("%d\n", Same<decltype(p), const int *>::v);
    return 0;
}
