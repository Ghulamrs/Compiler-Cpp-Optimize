// [stmt.ambig]/1: `T(x).f();` cannot be a declaration - no declarator goes on with `.` or `->`
// after its `)` - so it is an expression statement: a temporary of T built from x and its f
// called. Qualified and plain names, one and two arguments, a chained call, a template-id, and
// beside them the shapes that still declare: `T(x);`, `T (y) = z;` and `T(*p)(int);`.
extern "C" int printf(const char *, ...);

namespace ns {
struct Stream {
    int *p;
    Stream(int *q);
    void clear();
    Stream &self();
    Stream *ptr();
};
Stream::Stream(int *q) : p(q) {}
void Stream::clear() { *p = 0; }
Stream &Stream::self() { return *this; }
Stream *Stream::ptr() { return this; }
}

struct Formatter {
    int *out;
    int base;
    Formatter(int *o, int b);
    void run(const char *s, int extra);
};
Formatter::Formatter(int *o, int b) : out(o), base(b) {}
void Formatter::run(const char *s, int extra) { *out = base + s[0] + extra; }

template <class T> struct Box {
    T *at;
    Box(T *a) : at(a) {}
    void set(T v);
};
template <class T> void Box<T>::set(T v) { *at = v; }

struct Val {
    int v;
    Val();
    Val(int x);
};
Val::Val() : v(1) {}
Val::Val(int x) : v(x) {}

int twice(int n) { return 2 * n; }

int main() {
    int s = 5, r = 0, b = 0;
    ns::Stream(&s).clear();
    printf("%d\n", s);
    Formatter(&r, 3).run("a", 1);
    printf("%d\n", r);
    int *ps = &s;
    s = 9;
    ns::Stream(ps).self().clear();
    printf("%d\n", s);
    s = 4;
    ns::Stream(ps).ptr()->clear();
    printf("%d\n", s);
    Box<int>(&b).set(7);
    printf("%d\n", b);

    // These declare.
    Val(x);
    x.v = 6;
    Val (y) = x;
    int (*fp)(int) = twice;
    Val(*pv)(int);
    pv = 0;
    printf("%d %d %d %d\n", x.v, y.v, fp(5), pv == 0);
    return 0;
}
