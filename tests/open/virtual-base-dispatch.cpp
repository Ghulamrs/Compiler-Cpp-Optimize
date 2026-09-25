// Virtual inheritance is layout-only in cxx1: a class with a virtual base
// gets a vtable holding the vbase offset and its RTTI and nothing else - no
// function slots, no secondary vtable for the base subobject, no VTT, no
// virtual thunks, no deleting destructor - so a call through the virtual
// base reaches the base's function, and `delete` through it frees the wrong
// address. The third review's T9, true on every target (the C6000 faults in
// free, the hosts abort); clang prints the line below and "2332".
extern "C" int printf(const char *, ...);
struct VB { int x; virtual int f() { return 1; } virtual ~VB() { printf("~VB "); } };
struct VD : virtual VB { int y; int f() { return 2; } ~VD() { printf("~VD "); } };
struct VE : VD { int z; int f() { return 3; } };
int main() {
    VD d; VB *b = &d; int r1 = b->f();
    VE e; VB *be = &e; VD *de = &e; int r2 = be->f() * 10 + de->f() * 100;
    VB *h = new VD; int r3 = h->f() * 1000; delete h;
    printf("\n%d\n", r1 + r2 + r3);
    return 0;
}
