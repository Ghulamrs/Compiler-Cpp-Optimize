// [temp.res]/10 and [temp.dep.candidate]: a non-dependent call is bound where the template is written; a
// dependent one adds what argument-dependent lookup finds where it is instantiated, and nothing else.
#include <cstdio>
int g(int) { return 1; }
template <class T> int both(T t) { return g(t) * 100 + g(1); }
template <class T> struct Holder { int get(T t) { return g(t) + g('a'); } };
namespace lib {
    int twice(int v) { return v * 2; }
    template <class T> int use(T t) { return twice(t) + twice(1); }
    struct Tag {};
}
int g(double) { return 2; }
struct S {};
int g(S) { return 3; }
namespace lib {
    int twice(double) { return 1000; }
    int twice(Tag) { return 7; }
}
namespace other { struct X {}; int probe(X) { return 5; } }
template <class T> int adl(T t) { return probe(t); }
int main() {
    Holder<double> h;
    Holder<S> hs;
    std::printf("%d %d %d %d\n", both(2.5), both(S()), h.get(1.5), hs.get(S()));
    std::printf("%d %d %d\n", lib::use(1.5), lib::use(lib::Tag()), adl(other::X()));
    return 0;
}
