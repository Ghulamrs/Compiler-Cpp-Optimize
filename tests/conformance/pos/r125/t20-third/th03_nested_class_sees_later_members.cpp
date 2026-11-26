// [class.mem]/2: the enclosing class is complete inside the member function bodies of a nested class, so members declared after the nested class are visible.
#include <cstdio>
struct Outer {
    struct Nested { int get(const Outer &o) const { return o.later + o.alsoLater(); } };
    int later;
    int alsoLater() const { return 10; }
    Outer() : later(5) {}
};
int main() { Outer o; Outer::Nested n; std::printf("%d\n", n.get(o)); return 0; }
