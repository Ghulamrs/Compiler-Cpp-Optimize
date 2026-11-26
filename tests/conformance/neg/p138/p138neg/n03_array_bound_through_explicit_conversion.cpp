// [expr.const]/5: the conversion to an integer must not be explicit.
struct E { explicit constexpr operator int() const { return 2; } };
int a[E()];
int main() { return 0; }