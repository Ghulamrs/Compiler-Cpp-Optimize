// Aggregate initialisation of a struct whose members include a std::string, with braces and with T{...}; in a return statement.
#include <cstdio>
#include <string>
struct S { std::string s; int i; };
struct Nested { S inner; double d; };
S make() { return S{"made", 1}; }
S make2() { return {"made2", 2}; }
int main() {
    S a = {"local", 2}; S b{"brace", 3}; S c = make(); S d = make2(); Nested n = {{"nest", 4}, 2.5};
    std::printf("%s %d %s %d %s %d %s %d %s %g\n", a.s.c_str(), a.i, b.s.c_str(), b.i, c.s.c_str(), c.i, d.s.c_str(), d.i, n.inner.s.c_str(), n.d);
    return 0;
}
