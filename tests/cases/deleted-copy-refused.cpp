// [dcl.fct.def.delete]: a deleted copy constructor makes the class move-only or
// uncopyable, and `S b = a;` is the use that is refused. clang: "call to deleted
// constructor of 'S'" at `b` (6:12).
struct S { int v; S() : v(1) {} S(const S &) = delete; };
int main() {
    S a; S b = a;
    return b.v;
}
