// A friend of a derived class names a base's protected member through an object
// of that derived class - [class.access.base]/5 with [class.protected]. TI's cl6x
// accepts it; through a base object it is an error, which a refused case holds.
extern "C" int printf(const char *, ...);

struct B { protected: int v; };
struct D : B {
    D(int k) { v = k; }
    friend int get(const D &d) { return d.v; }
};
struct E : D { E() : D(9) { } };   // a further-derived object reached by D's friend

template <int N> struct TB { protected: int w[N]; };
template <int N> struct TD : TB<N> {
    TD() { for (int i = 0; i < N; i++) this->w[i] = i * 10; }
    friend int at(const TD<N> &t, int i) { return t.w[i]; }
};

int main() {
    D d(7);
    E e;
    TD<3> t;
    printf("%d %d %d %d\n", get(d), get(e), at(t, 1), at(t, 2));
    return 0;
}
