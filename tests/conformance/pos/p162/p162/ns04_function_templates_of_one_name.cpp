// [namespace.qual] and [basic.lookup.qual]/4 for function templates of one name and one signature in three namespaces,
// `::g` passing over a nearer one.
#include <cstdio>
template <class T> int g(T) { return 30; }
namespace a { template <class T> int g(T) { return 10; } }
namespace b {
    template <class T> int g(T) { return 20; }
    int near() { return g(1); }
    int far(int v) { return ::g(v); }
    int farExplicit(int v) { return ::g<long>(v); }
}
int main() {
    std::printf("%d %d %d\n", a::g(1), b::g(1), g(1));
    std::printf("%d %d %d %d\n", ::g(2), b::near(), b::far(3), b::farExplicit(4));
    return 0;
}
