#include <cstdio>

// [over.match.funcs]/5 and [over.ics.ref]/1: a member operator of a base binds a derived object by a
// derived-to-base Conversion, as a non-member's base reference does - so the better second argument decides.
struct Base {
    int operator<<(const void *) { return 1; }
};
struct Derived : Base {};
int operator<<(Base &, const wchar_t *) { return 2; }

typedef int (*Fn)(int);
static int twice(int v) { return v * 2; }
template <class T> T same(T v) { return v; }
struct Sink {
    int got;
    Sink() : got(0) {}
    Sink &operator<<(Fn f) { got = f(21); return *this; }
};

int main() {
    Derived d;
    const int a = d << L"w";
    const int b = d << static_cast<const void *>(0);
    Sink s;
    s << twice;
    const int c = s.got;
    s << same;
    std::printf("%d %d %d %d\n", a, b, c, s.got);
    return 0;
}
