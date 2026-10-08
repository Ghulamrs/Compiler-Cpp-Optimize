// [dcl.fct.def.default]/1: only a special member function may be defaulted;
// `int f() = default;` names nothing the compiler could write. clang: "only
// special member functions may be defaulted" (4:22).
struct S { int f() = default; };
int main() { S s; return s.f(); }
