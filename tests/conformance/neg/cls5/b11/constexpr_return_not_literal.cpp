// [dcl.constexpr]/3: a constexpr function's return type is a literal type; S has a user-provided
// constructor that is not constexpr and is not an aggregate.
struct S {
    S() : v(1) {}
    int v;
};
constexpr S make() { return S(); }
int main() {
    return 0;
}
