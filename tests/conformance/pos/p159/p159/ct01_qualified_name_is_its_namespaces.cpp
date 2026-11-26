#include <cstdio>

// [namespace.qual]: k::collate is k's - the class template of that name in the enclosing namespace is not looked
// at, whether the name is used from inside that namespace or from outside it.
namespace n {
template <class T> struct collate { T t; };
template <class T> T twice(T v) { return v + v; }
namespace k {
enum e { collate = 8, twice = 1 };
}
struct S { static const int v = k::collate; };
int f() { return k::collate + k::twice; }
}

int main() {
    n::collate<int> c;
    c.t = n::twice(2);
    std::printf("%d %d %d %d\n", n::k::collate, n::S::v, n::f(), c.t);
    return 0;
}
