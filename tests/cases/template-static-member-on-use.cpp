// [temp.inst]/3: a class template's static data member is defined only where it
// is used. Instantiating C<int> defines none of its statics; each one an evaluated
// expression names - read, address taken, bound to a reference, inside a member
// function that is called, inside another static's initialiser - is defined, and
// an unused one with a constructor never runs it.
extern "C" int printf(const char *, ...);

struct Loud {
    int v;
    Loud(int x);
};
Loud::Loud(int x) : v(x) { printf("Loud(%d)\n", x); }

template <class T> struct C {
    static int read;
    static int addressed;
    static int bound;
    static int viaMember;
    static int viaOther;
    static int other;
    static int unused;
    static int sizedOnly;
    static Loud built;
    static Loud neverBuilt;
    int get() const { return viaMember; }
};

template <class T> int C<T>::read = 1;
template <class T> int C<T>::addressed = 2;
template <class T> int C<T>::bound = 3;
template <class T> int C<T>::viaMember = 4;
template <class T> int C<T>::viaOther = 5;
template <class T> int C<T>::other = C<T>::viaOther * 10;
template <class T> int C<T>::unused = 6;
template <class T> int C<T>::sizedOnly = 7;
template <class T> Loud C<T>::built(8);
template <class T> Loud C<T>::neverBuilt(9);

int main() {
    C<int> c;
    C<long> d;
    (void)d;
    int *p = &C<int>::addressed;
    int &r = C<int>::bound;
    r += 30;
    printf("%d %d %d %d %d\n", C<int>::read, *p, C<int>::bound, c.get(), C<int>::other);
    printf("%d %d\n", (int)sizeof(C<int>::sizedOnly), C<int>::built.v);
    return 0;
}
