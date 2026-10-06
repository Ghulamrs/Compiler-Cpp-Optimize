// An inline function nothing in a unit uses is not emitted there - [basic.def.odr]/3 asks for its
// definition only where it is odr-used, and clang, g++ and cl emit nothing for one. cpp11 emitted
// every one, so a header's inline wrapper over a function the program never links (here
// `never_linked`) left an undefined symbol in every unit that included it. The second translation
// unit, unused-inline-not-emitted.part.cpp, holds the same header and uses nothing from it.
extern "C" int printf(const char *, ...);
extern "C" int never_linked(int);
extern "C" int linked(int x) { return x * 3; }

// The header both units include.
inline int wrapper(int x) { return never_linked(x); }
static inline int hidden(int x) { return never_linked(x) + 1; }
inline int used(int x) { return linked(x) + 1; }
inline int chained(int x) { return used(x) * 2; }
struct Tool {
    int value;
    int unusedMember() const { return never_linked(value); }
    int usedMember() const { return linked(value); }
    static int unusedStatic(int x) { return never_linked(x); }
};
namespace lib { inline int unusedInNamespace(int x) { return never_linked(x); } }
// Reached only through an unused inline function: dropped with it.
inline int onlyFromUnused(int x) { return never_linked(x); }
inline int unusedCaller(int x) { return onlyFromUnused(x); }
// Its address taken: odr-used, and emitted.
inline int addressed(int x) { return x + 7; }

int partValue();

int main() {
    Tool t = { 4 };
    int (*f)(int) = addressed;
    printf("%d %d %d %d %d\n", used(2), chained(1), t.usedMember(), f(1), partValue());
    return 0;
}
