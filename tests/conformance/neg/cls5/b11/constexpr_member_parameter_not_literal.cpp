// [dcl.constexpr]/3: a constexpr static member function taking a non-literal class by value (a pointer to an array of them is literal).
struct N {
    N() {}
};
struct Q {
    constexpr int count(const N (*)[2]) const { return 2; }
    constexpr static int take(N n) { return 1; }
};
int main() {
    return 0;
}
