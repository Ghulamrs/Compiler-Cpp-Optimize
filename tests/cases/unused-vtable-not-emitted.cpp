// A polymorphic class with no key function - every virtual inline - has its vtable emitted
// only where it is needed: clang and cl write the table, and so the virtuals it names, where
// a constructor or destructor stores it, and a type_info alone where one is named. cpp11
// wrote the table of every such class at completion, so the virtuals of a class nothing
// builds were emitted - here over `never_linked`, which nothing defines, and the link failed.
extern "C" int printf(const char *, ...);
extern "C" int never_linked(int);

// Nothing builds these: no table, no virtuals.
struct Unused {
    int v;
    virtual int f() { return never_linked(v); }
    virtual ~Unused() { never_linked(0); }
};
struct UnusedDerived : Unused {
    int f() { return never_linked(v + 1); }
};
struct ImplicitOnly {
    virtual int g() { return never_linked(2); }
};
struct ImplicitDerived : ImplicitOnly {
    int g() { return never_linked(3); }
};

// Built: the table and its virtuals.
// Their constructors are out of line, so clang writes C1 beside C2 and the names compare.
struct Built {
    int k;
    Built();
    virtual int f() { return k; }
};
Built::Built() : k(4) {}
// Built only as a base: the derived constructor stores the base's table too.
struct OnlyBase {
    OnlyBase();
    virtual int g() { return 5; }
};
OnlyBase::OnlyBase() {}
struct Derived : OnlyBase {
    Derived();
    int g() { return 6; }
};
Derived::Derived() {}
// Caught and never built: its type_info alone.
struct Caught {
    virtual int f() { return never_linked(7); }
};
// A key function defined here: the table is this unit's, built or not.
struct Key {
    virtual int f();
    virtual int h() { return 9; }
};
int Key::f() { return 8; }

int use(OnlyBase *b) { return b->g(); }

int main() {
    Built b;
    Derived d;
    int caught = 0;
    try {
        throw 1;
    } catch (Caught &) {
        caught = 1;
    } catch (int n) {
        caught = n + 10;
    }
    printf("%d %d %d %d\n", b.f(), use(&d), d.OnlyBase::g(), caught);
    return 0;
}
