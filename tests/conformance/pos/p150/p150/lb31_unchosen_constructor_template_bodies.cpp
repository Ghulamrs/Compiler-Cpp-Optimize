// [temp.inst]/3: a constructor template's body is instantiated for the candidate chosen, not for every one considered.
#include <cstdio>
#include <cstddef>
template <class T> struct SP {
    T *p;
    explicit SP(T *q) : p(q) {}
    template <class D> SP(std::nullptr_t, D d) : p(nullptr) { d(p); }
    template <class Y> SP(const SP<Y> &o, T *q) : p(q) { (void)o; }
    template <class Y, class D, class A> SP(Y *y, D d, A a) : p(y) { d(y); a.allocate(1); }
};
struct S { int a; int b; };
int main() {
    S s = {1, 2};
    SP<S> sp(&s);
    SP<int> alias(sp, &sp.p->b);
    std::printf("%d\n", *alias.p);
    return 0;
}
