// [dcl.constexpr]/4: each of a constexpr constructor's parameter types is a literal type.
struct Seed {
    Seed() {}
    int v = 2;
};
struct P {
    int x;
    constexpr P(Seed s) : x(3) {}
};
int main() {
    return 0;
}
