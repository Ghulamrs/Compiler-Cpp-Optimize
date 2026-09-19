// A12/A13: a file-scope scalar or trivial class whose initialiser does not
// fold is stored before main, in declaration order with the objects that
// have constructors; a template's static member under its once-guard.
extern "C" int printf(const char *, ...);
int init(int v) { printf("init %d\n", v); return v; }
struct S { S(int k) { printf("S %d\n", k); } };
struct P { int a; double b; };
P make() { P p; p.a = 7; p.b = 1.5; return p; }
int gA = init(1);
S s1(2);
double gD = gA * 1.5;
int gB = init(3);
const char *gS = gA > 0 ? "yes" : "no";
P g = P();
P h = make();
struct Q { int k; Q() : k(5) {} };
Q q = Q();
template <class T> struct Tm { static T st; };
template <class T> T Tm<T>::st = T();
extern int gE;
int gE = init(4);
int main() {
    Tm<P>::st.a = 3;
    int i = Tm<int>::st;
    printf("%d %.1f %d %s %d %.1f %d %.1f %d %d %d %d\n", gA, gD, gB, gS,
           g.a, g.b, h.a, h.b, q.k, Tm<P>::st.a, i, gE);
    return 0;
}
