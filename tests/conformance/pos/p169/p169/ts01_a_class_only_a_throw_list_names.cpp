// [except.spec]: a class a `throw(T...)` list names and nothing else in the program throws, catches or asks the
// typeid of. Where the list is the unwinder's table (the C6747), the table names the class's type_info, and
// until 1.68 that type_info was not emitted: the program did not link (q200 b33, with 256 of them).
#include <cstdio>

struct OnlyListed { int k; };
struct AlsoListed { virtual ~AlsoListed() {} int k; };
struct Thrown { int k; };
namespace lib { struct Deep { struct Inner { int k; }; }; }

void f(int k) throw(OnlyListed, AlsoListed, lib::Deep::Inner, Thrown) {
    if (k > 0) { Thrown t = { k }; throw t; }
}

struct S {
    int g(int k) const throw(OnlyListed *, const AlsoListed &, Thrown) {
        if (k > 1) { Thrown t = { k * 2 }; throw t; }
        return k;
    }
};

int main() {
    f(0);
    try { f(7); } catch (Thrown &t) { std::printf("caught %d\n", t.k); }
    S s;
    std::printf("%d\n", s.g(1));
    try { s.g(5); } catch (const Thrown &t) { std::printf("caught %d\n", t.k); }
    return 0;
}
