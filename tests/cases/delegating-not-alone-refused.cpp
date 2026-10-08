// [class.base.init]/6: a mem-initialiser list that names the class itself has
// no other entry - the target constructor builds the members. clang: "an
// initializer for a delegating constructor must appear alone" at 6:26.
struct S {
    int n, m;
    S(int a) : n(a), m(0) {}
    S() : S(1), m(2) {}
};

int main(void) { S s; return s.n; }
