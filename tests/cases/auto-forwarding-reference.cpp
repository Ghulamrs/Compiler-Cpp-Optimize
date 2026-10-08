// **`auto &&` is a forwarding reference too** - [dcl.spec.auto]/7 deduces `auto` as a
// template parameter would be from a call, so an lvalue initialiser gives `U &` and a
// prvalue gives `U &&`. `auto &&` used to come out `U &` whatever it was given, which took
// an lvalue by accident and refused `auto &&b = 5`. A range-for's `auto &&` sees an lvalue.
//
// Measured against clang -std=c++11 -pedantic-errors.
extern "C" int printf(const char *, ...);

int kind(int &)  { return 1; }
int kind(int &&) { return 2; }
int make() { return 9; }

int main() {
    int i = 3;
    auto &&a = i;
    a = 4;
    auto &&b = 5;
    auto &&c = make();
    b = b + 1;
    const int ci = 7;
    auto &&d = ci;
    printf("%d %d %d %d\n", i, b, c, d);
    printf("%d %d %d\n", kind(static_cast<decltype(a) &&>(a)), kind(static_cast<decltype(b) &&>(b)),
           (int)sizeof(d));
    int arr[3] = { 1, 2, 3 };
    for (auto &&x : arr) x *= 2;
    printf("%d %d %d\n", arr[0], arr[1], arr[2]);
    if (auto &&e = i) printf("%d\n", e);
    return 0;
}
