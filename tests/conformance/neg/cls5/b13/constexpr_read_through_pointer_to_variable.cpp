// [expr.const]/2: reading a non-const global through a constexpr pointer is no constant expression.
int g = 1;
constexpr int *pg = &g;
constexpr int read = *pg;
int main() {
    return read;
}
