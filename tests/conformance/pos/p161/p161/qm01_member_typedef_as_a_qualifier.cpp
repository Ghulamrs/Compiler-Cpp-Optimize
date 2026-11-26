// [basic.lookup.classref]/4: the name before `::` after `.` or `->` is looked up in the class of the object first, so a
// member typedef names the base; and a class names its own private base so, [class.access.base].
#include <cstdio>
struct Base { int f() { return 1; } int v; };
struct Der : Base { typedef Base base; int f() { return 2; } };
template <class T> struct Tm : T { typedef T super; int g() { return super::f() + this->super::f(); } };
struct Hidden { int h(void *) { return 3; } };
class Own : private Hidden { typedef Hidden base_; public: int k() { return base_::h(this) + this->Hidden::h(this); } };
template <class A, class... B> struct S { int f() { return 1; } int h(void *) { return 3; } };
template <class A> struct S<A> { int f() { return 2; } int h(void *) { return 4; } };
template <class A, class... B> class E : private S<A, B...> {
    typedef S<A, B...> base_;
public:
    int k() noexcept { return base_::h(this); }
};
int main() {
    Der d;
    d.v = 7;
    Der *p = &d;
    int a = d.base::f();
    int b = p->base::f();
    int c = d.Der::base::f();
    int e = d.base::v;
    std::printf("%d %d %d %d\n", a, b, c, e);
    Tm<Base> t;
    Own o;
    E<int> x;
    E<int, char> y;
    std::printf("%d %d %d %d\n", t.g(), o.k(), x.k(), y.k());
    return 0;
}
