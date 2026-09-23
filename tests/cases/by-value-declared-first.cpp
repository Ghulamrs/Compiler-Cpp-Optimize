// A by-value parameter with a destructor, in a function that was declared
// before it was defined - every function a header declares and a .cpp defines.
//
// On Microsoft the callee destroys such a parameter (by-value-destructor.cpp
// measures it), so the parameter loop registers it as alive when the target is
// x86_64-windows. Until 2026-09-17 it did so for a prototype too, and a
// prototype has no body to pop it: the entry stayed on the list and the next
// definition - the same function or any other - destroyed that slot as its
// own, a second `??1` on the normal path. cl prints one `~NT` per copy; cpp11
// printed nothing and died. The ledger counts constructions and destructions,
// and the three shapes are: declared then defined; a prototype of another
// function ahead of a definition; and a prototype of two by-value parameters
// and a reference, ahead of a function that has none of its own to destroy.
extern "C" int printf(const char *, ...);

static int made = 0, gone = 0;

struct NT {
    int x;
    NT(int v);
    NT(const NT &o);
    ~NT();
};
NT::NT(int v) : x(v) { ++made; }
NT::NT(const NT &o) : x(o.x) { ++made; }
NT::~NT() { ++gone; printf("~NT %d\n", x); }

int takeNT(NT);                    // declared, then defined below
int other(NT);                     // a prototype ahead of a definition; never defined
int pair(NT, NT, const NT &);      // two by value, one by reference
int plain(int);                    // no parameter to destroy

int takeNT(NT n) { printf("NT %d\n", n.x); return n.x; }

int noneOfItsOwn(int v) { return v + 1; }   // follows `other`'s prototype

int plain(int v) { return v * 2; }          // follows `pair`'s prototype

int main() {
    NT a(14);
    int r = takeNT(a);
    printf("r=%d made=%d gone=%d\n", r, made, gone);
    printf("%d %d made=%d gone=%d\n", noneOfItsOwn(1), plain(3), made, gone);
    return 0;
}
