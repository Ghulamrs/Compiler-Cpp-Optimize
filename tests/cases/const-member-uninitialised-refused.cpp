// [class.base.init]/8: a const member a written constructor's list leaves out,
// with no initialiser of its own, is ill-formed - nothing after the list can
// give it a value. clang: "constructor for 'S' must explicitly initialize the
// const member 'k'", at 1:25 of the one-line form; the refusal is at the
// constructor. The neighbour const-member-init.cpp is the named member, which
// works.
struct S {
    const int k;
    int n;
    S() : n(1) {}
};

int main(void) {
    S s;
    return s.n;
}
