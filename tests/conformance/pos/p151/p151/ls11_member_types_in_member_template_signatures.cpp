// [class.mem], [temp.mem]: a member template's parameters name the class's member types, in a class template
// and in an ordinary class.
#include <cstdio>
template <class C> struct B {
    typedef const C *const_iterator;
    typedef C *iterator;
    C d[4];
    template <class It> iterator put(const_iterator pos, It v) { iterator p = d + (pos - d); *p = (C)v; return p; }
};
struct P {
    typedef const int *ci;
    int d[4];
    template <class It> int put(ci pos, It v) { return (int)(pos - d) + (int)v; }
};
int main() {
    B<int> b;
    *b.put(b.d + 1, 7.5) += 1;
    P p;
    std::printf("%d %d\n", b.d[1], p.put(p.d + 2, 3));
    return 0;
}
