// **Deleting through a pointer to a *virtual* base whose destructor is
// virtual.** `L : virtual B` and `M : L`; `delete p` where `p` is an `L *`
// holding an M reaches M's deleting destructor through L's table - a slot
// L's destructor claims because B's is virtual, which used to go unclaimed
// for a class whose polymorphism comes down a virtual base, so both targets
// ran `~L` and never `~M`. The read of `n` goes through the vbase offset of
// the most derived object, which the construction vtable M's constructor
// hands L's C2 in its VTT - the `+B6` is M's mem-initialiser, not L's.
// Closed 2026-09-15 with the virtual inheritance round; x86_64-windows
// refuses a polymorphic virtual base by name (the .notarget beside this).
extern "C" int printf(const char *, ...);

struct B {
    int n;
    B(int v) : n(v) { printf("+B%d ", v); }
    virtual ~B() { printf("-B%d ", n); }
};
struct L : virtual B { L(int v) : B(v) { printf("+L "); } ~L() { printf("-L "); } };
struct M : L { M(int v) : L(v), B(v + 1) { printf("+M "); } ~M() { printf("-M "); } };

int main() {
    L *p = new M(5);
    printf("| %d\n", p->n);
    delete p;
    printf("\n");
    return 0;
}
