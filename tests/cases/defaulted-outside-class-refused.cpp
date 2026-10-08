// `= default` on a declaration after the first is legal C++11 - [dcl.fct.def.default]/4
// makes such a member user-provided - and refused by name here: the implicit member
// stands in for one defaulted inside its class only. clang accepts it.
struct S { int v; S(); };
S::S() = default;
int main() { S s; s.v = 1; return s.v - 1; }
