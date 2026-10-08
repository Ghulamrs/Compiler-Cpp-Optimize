// noexcept, both of them: the specifier and the operator.
//
// **In C++11 the exception specification is not part of the function's type.**
// Measured: `void f() noexcept` and `void f()` both mangle to `_Z1fv` on
// Itanium and `?f@@YAXXZ` on Microsoft. So no name, no signature match and no
// overload set changes - which is most of why this rung is small. C++17 made
// it part of the type; this compiler targets C++11.
//
// What the specifier buys is a compile-time answer, and the operator is what
// asks for it. `noexcept(e)` does not run `e`: it is parsed for its meaning
// and thrown away, the way `sizeof`'s operand is, and what is kept is whether
// anything in it could throw. That is counted *during* the parse rather than
// by walking the tree afterwards, because every call already passes through
// one place and so does every `throw`.
//
// **The promise is not enforced at run time** - a throw escaping a `noexcept`
// function propagates here where it should call std::terminate. That is
// recorded in docs/CONFORMANCE.md rather than half-built.

extern "C" int printf(const char *, ...);

int quiet(void) noexcept { return 1; }
int loud(void) { return 2; }

int alsoQuiet(void) noexcept(true) { return 3; }
int notQuiet(void) noexcept(false) { return 4; }

// `throw()` is the C++03 spelling of the same promise and is taken as one.
int oldStyle(void) throw() { return 5; }

// A constant expression decides it, which is what makes the form worth having.
int computed(void) noexcept(sizeof(int) == 4) { return 6; }

// Declared here and defined below - and the definition has to say it again.
// The specification is not part of the type, so nothing else holds the two
// together; leaving it off the definition is an error in both compilers, which
// noexcept-mismatch-refused.cpp pins.
int declaredFirst(void) noexcept;
int declaredFirst(void) noexcept { return 7; }

struct Box {
    int v;
    // On a member the specification follows the constness, which is the order
    // C++ writes them in.
    int get(void) const noexcept { return v; }
    int grow(void) { return ++v; }
    explicit Box(int a) noexcept;
    ~Box(void) noexcept;
};
// Defined out of line for the reason CLAUDE.md gives under `explicit`: for a
// constructor or destructor written *inside* its class, clang on x86_64-linux
// emits and calls the base-object C2/D2 where cxx1 uses the complete-object
// C1/D1 - both self-consistent, and `names.sh` reads it as a disagreement
// about names when it is one about emission.
Box::Box(int a) noexcept { v = a; }
Box::~Box(void) noexcept {}

int throughAnObject(const Box &b) noexcept { return b.get(); }

int main(void) {
    Box b(10);

    // The operator, on things that promise and things that do not.
    printf("%d %d %d %d\n",
           noexcept(quiet()), noexcept(loud()),
           noexcept(alsoQuiet()), noexcept(notQuiet()));
    printf("%d %d %d\n",
           noexcept(oldStyle()), noexcept(computed()), noexcept(declaredFirst()));

    // A member call, and one that reaches a member through a noexcept
    // function - the count is about what the expression touches, not about
    // where it is written.
    printf("%d %d %d\n",
           noexcept(b.get()), noexcept(b.grow()), noexcept(throughAnObject(b)));

    // Nothing to call at all, so nothing to throw. `sizeof` never evaluates
    // its operand, so even a throwing call inside one is quiet.
    int n = 1;
    printf("%d %d %d\n", noexcept(n + 1), noexcept(sizeof(int)), noexcept(true));

    // **A call through a pointer promises nothing**, because the promise is
    // not part of the type in C++11 - so this is false even though `quiet` is
    // what the pointer holds.
    int (*through)(void) = quiet;
    printf("%d\n", noexcept(through()));

    // And the functions themselves still work, which is the other half.
    printf("%d %d %d %d %d %d %d\n", quiet(), loud(), alsoQuiet(), notQuiet(),
           oldStyle(), computed(), declaredFirst());
    int got = b.get();
    int grown = b.grow();
    printf("%d %d\n", got, grown);
    return 0;
}
