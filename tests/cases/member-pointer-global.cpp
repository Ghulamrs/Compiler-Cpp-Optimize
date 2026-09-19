// A global of pointer-to-member type: its Microsoft name carries the class
// again as the storage class, `EQ<class>@` after the type (the cl review's
// A7 - cl spells ?gpm@@3PEQNest@@HEQ1@, and this wrote ...HA).
extern "C" int printf(const char *, ...);
struct Nest { struct In { int v; }; In in; int k; int g(In, In *) { return in.v + 1; } };
int Nest::*gpm;
int (Nest::*gpmf)(Nest::In, Nest::In *);
int main() {
    gpm = &Nest::k;
    gpmf = &Nest::g;
    Nest n; n.in.v = 4; n.k = 9;
    Nest::In arg;
    int a = (n.*gpmf)(arg, &arg);
    printf("%d %d\n", a, n.*gpm);
    return 0;
}
