// A15: two function templates both deduce, and [temp.func.order] picks the
// more specialized - f(T *) over f(T), f(T, T) over f(T, U), f(const T &)
// over f(T) is not ordered and stays ambiguous; an ordinary function still
// wins a tie against any specialization.
extern "C" int printf(const char *, ...);
template <class T> int kind(T) { return 0; }
template <class T> int kind(T *) { return 1; }
template <class T, class U> int same(T, U) { return 0; }
template <class T> int same(T, T) { return 1; }
template <class T> int plain(T) { return 0; }
int plain(int) { return 2; }
struct S { int v; };
template <class T> int mem(T) { return 0; }
template <class T> int mem(T S::*) { return 1; }
int main() {
    int z = 0; S s; s.v = 4; int S::*pv = &S::v;
    printf("%d %d %d %d %d %d %d\n", kind(1), kind(&z), same(1, 2.0), same(1, 2),
           plain(3), plain('c'), mem(pv));
    return 0;
}
