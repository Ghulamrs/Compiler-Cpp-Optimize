// **Virtual inheritance proper, on the Itanium ABI** - the third review's
// T9, closed 2026-09-15. A class with a virtual base used to get a vtable
// holding the vbase offset and its RTTI and nothing else: no function slots,
// no secondary table for the base subobject, no VTT, no virtual thunks, no
// deleting destructor - so a call through the virtual base reached the base's
// function, and `delete` through it freed the wrong address.
//
// What is written now, measured against clang's -fdump-vtable-layouts and
// its `_ZTT`/`_ZTC` symbols on x86_64: `VD::f` and `~VD` take new slots in
// VD's primary table, the VB-in-VD secondary table holds vcall offsets and
// the virtual thunks `_ZTv0_n24_N2VD1fEv` and `_ZTv0_n32_N2VDD1Ev`/`D0Ev`,
// `_ZTT2VD` names the address points, and VE's constructor hands VD's C2 a
// sub-VTT pointing into the construction vtable `_ZTC2VE0_2VD`. Through a
// `VB *` every call lands on the most derived class's function, and `delete`
// on the deleting destructor that frees the complete object.
//
// The Microsoft ABI dispatches through a polymorphic virtual base with
// vtordisp fields and thunks of its own, measured for no class yet; that
// target refuses this by name (the .notarget beside this file).
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
