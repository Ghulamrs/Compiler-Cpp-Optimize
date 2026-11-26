// [temp.res]/8 with [temp.mem]: member templates of an ordinary class that nothing instantiates, overloaded and
// naming the class's members, are no error - the probe reads each in its class.
#include <cstdio>
template <bool> struct Tag {};
struct V {
    int n;
    template <class It> void add(It a, It b, Tag<true>) { n = a * 100 + b; }
    template <class It> void add(It, It, Tag<false>) { n = -1; }
    template <class It> int twice(It a) const { return n + 2 * a; }
};
int main() {
    V v;
    v.n = 4;
    std::printf("%d\n", v.n);
    return 0;
}
