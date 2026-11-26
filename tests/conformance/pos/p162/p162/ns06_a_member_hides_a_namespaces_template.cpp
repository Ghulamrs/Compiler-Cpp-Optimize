// [basic.lookup.unqual]/8: a class's data member hides a namespace's name - `max` beside std::max, as <ratio>'s own has it.
#include <algorithm>
#include <cstdio>
#include <ratio>
struct S {
    static const int max = 5;
    static const int min = 2;
    int count;
    static_assert(min <= max / 2, "in the class");
    int f() const { return max + min + count; }
};
template <int N> struct T {
    static const int max = N;
    static_assert(1 <= max / 2, "in a template");
    int g() const { return max * 2; }
};
int main() {
    typedef std::ratio_multiply<std::ratio<1, 2>, std::ratio<4, 3> > R;
    S s = { 1 };
    T<4> t;
    std::printf("%d %d %d %d\n", (int)R::num, (int)R::den, s.f(), t.g());
    return 0;
}
