// An overloaded operator on an enumeration must be found by ADL ([over.match.oper]/3); if the built-in is used instead the result silently differs.
#include <cstdio>
namespace N { enum Flag { A = 1, B = 2 }; Flag operator|(Flag, Flag) { return Flag(99); } int operator+(Flag, int) { return -1; } }
enum Global { G1 = 4, G2 = 8 };
Global operator&(Global, Global) { return Global(77); }
int main() {
    N::Flag f = N::A | N::B;
    std::printf("%d %d %d\n", (int)f, (int)(N::A | N::B), N::A + 1);
    std::printf("%d\n", (int)(G1 & G2));
    return 0;
}
