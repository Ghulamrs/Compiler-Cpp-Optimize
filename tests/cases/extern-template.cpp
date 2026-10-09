// `extern template struct S<int>;` - [temp.explicit]/2 - promises a definition in another
// translation unit and suppresses the implicit instantiation here. cpp11 reads it and suppresses
// nothing: every specialization it emits is mergeable (weak, COMDAT), so this unit's copy folds
// with the other's at the link, which is what the two files prove. CONFORMANCE.md records it.
extern "C" int printf(const char *, ...);

template <class T> struct S { T v; T twice() const { return v * 2; } };
template <class T> T add(T a, T b) { return a + b; }

extern template struct S<int>;
extern template int add<int>(int, int);
extern template struct S<double>;

int other(int n);

int main() {
    S<int> s;
    s.v = 21;
    S<double> d;
    d.v = 1.25;
    int sum = add(3, 4);
    printf("%d %.2f %d\n", s.twice(), d.twice(), sum);
    int o = other(5);
    printf("%d\n", o);
    return 0;
}
