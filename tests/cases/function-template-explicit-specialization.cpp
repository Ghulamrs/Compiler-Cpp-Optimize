// **An explicit specialization of a function template** - [temp.expl.spec]: `template <> int
// twice<int>(int)` and the same with the arguments deduced from its parameters. It is the
// specialization the primary would have made - the same key and the same linker name, the
// primary's pattern plus the arguments - with its own body in place of the primary's.
// It was refused by name until 2026-10-08.
//
// Measured against clang -std=c++11 -pedantic-errors. One call per statement.
extern "C" int printf(const char *, ...);

template <class T> T twice(T x) { return x + x; }
template <> int twice<int>(int x) { return x * 3; }      // written out
template <> double twice(double x) { return x * 10; }     // deduced from the parameter

template <class T> int size(const T &) { return (int)sizeof(T); }
template <> int size<char>(const char &) { return 100; }

struct Pt { int x, y; };
template <> int size(const Pt &p) { return p.x + p.y; }

// Two primaries of one name: each specialization belongs to the one its parameters match.
template <class T> int pick(T) { return 1; }
template <class T> int pick(T *) { return 2; }
template <> int pick(int *) { return 20; }
template <> int pick<char>(char) { return 10; }

int main() {
    printf("%d %g %ld\n", twice(4), twice(1.5), twice(5L));
    Pt p = { 3, 4 };
    printf("%d %d %d\n", size('c'), size(1.0), size(p));
    int n = 0;
    double d = 0;
    printf("%d %d %d %d\n", pick(n), pick(&n), pick('c'), pick(&d));
    return 0;
}
