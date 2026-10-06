// [class.static.data]: a static data member may be a reference, defined out of
// line as `int &S::r = g;`. Bound in the image to a global's address, or before
// main to anything else; read through by every way of naming it.
extern "C" int printf(const char *, ...);

int g = 5;
int arr[3] = { 10, 20, 30 };
int other = 40;
int &pick() { return other; }

struct S {
    static int &r;
    static const int &cr;
    static int &elem;
    static int &dyn;
    int get() const { return r + cr; }      // unqualified, inside a member
    void bump() { r += 1; }
};

int &S::r = g;
const int &S::cr = g;
int &S::elem = arr[1];
int &S::dyn = pick();

template <class T> struct Holder { static T &ref; };
double gd = 2.5;
template <class T> T &Holder<T>::ref = gd;

int main() {
    S::r = 7;
    decltype(S::r) q = S::r;
    q = 9;
    S s;
    s.bump();
    S::elem += 1;
    S::dyn *= 2;
    S *ps = &s;
    ps->dyn += 1;
    s.elem += 100;
    Holder<double>::ref += 1;
    int *p = &S::r;
    printf("%d %d %d %d\n", g, S::cr, s.get(), *p == g);
    printf("%d %d %d %g\n", arr[1], other, (int)sizeof(decltype(S::r)), gd);
    return 0;
}
