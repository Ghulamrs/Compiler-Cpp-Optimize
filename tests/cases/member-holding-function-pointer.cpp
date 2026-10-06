// A data member holding something callable, called by its bare name inside a member function.
// [class.mfct.non-static]/3 makes `compare_(a, b)` mean `(*this).compare_(a, b)`, an ordinary call
// through the pointer the member holds; cpp11 sent the name to the free-function table and said it
// was not declared. [basic.lookup.unqual]: the member `pick` hides the namespace-scope `pick`.
extern "C" int printf(const char *, ...);
static int less(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }
static int add(int a, int b) { return a + b; }
static int sub(int a, int b) { return a - b; }
static int mul(int a, int b) { return a * b; }
int pick(int a, int b) { return 100 + a + b; }
struct Functor { int k; int operator()(int a) const { return a * k; } };
struct Sorter {
    int (*compare_)(const void *, const void *);
    static int (*fallback_)(int, int);
    int (*pick)(int, int);
    Functor twice;
    Sorter(int (*c)(const void *, const void *)) : compare_(c), pick(sub) { twice.k = 2; }
    int one(int a, int b) { return compare_(&a, &b); }
    int two(int a, int b) { return this->compare_(&a, &b); }
    int three(int a, int b) { return fallback_(a, b); }
    int four(int a, int b) { return pick(a, b); }
    int five(int a) const { return twice(a); }
    int six(int a, int b) { int (*r)(int, int) = mul; return r(a, b); }
    int seven(int a, int b) { return [this](int x, int y) { return pick(x, y); }(a, b); }
};
int (*Sorter::fallback_)(int, int) = add;
struct Derived : Sorter {
    Derived() : Sorter(less) {}
    int eight(int a, int b) { return compare_(&a, &b) + pick(a, b); }
};
int main() {
    Sorter s(less);
    Sorter *p = &s;
    int x = 3, y = 8;
    printf("%d %d %d %d\n", s.one(1, 2), s.two(5, 3), s.three(4, 6), s.four(9, 2));
    printf("%d %d %d %d\n", s.five(21), s.six(6, 7), s.seven(10, 4), Derived().eight(2, 2));
    printf("%d %d %d %d\n", s.compare_(&x, &y), p->compare_(&y, &x), Sorter::fallback_(1, 2), p->pick(1, 1));
    int (*f)(int, int) = add;
    printf("%d %d %d\n", pick(1, 2), [f](int a) { return f(a, 1); }(5), [=](int a) { return f(a, 2); }(5));
    return 0;
}
