// Built-in operator candidates [over.built] with class operands convertible to arithmetic; mixed class/enum operands; conditional operator with class and conversions.
#include <cstdio>
struct N { int v; operator int() const { return v; } };
struct F { double v; operator double() const { return v; } };
struct B2 { bool b; operator bool() const { return b; } };
int main() {
    N a = {3}, b = {4}; F f = {0.5}; B2 t = {true}, u = {false};
    std::printf("%d %d %g %d\n", a + b, a * 2, a + f, a < b);
    std::printf("%d %d %d\n", t && u, t || u, !u);
    int arr[5] = {0, 1, 2, 3, 4}; std::printf("%d\n", arr[a]);
    std::printf("%d %g\n", a ? 1 : 0, t ? f : 1.0);
    int x = a; x += b; x <<= N{1}; std::printf("%d\n", x);
    switch (a) { case 3: std::printf("three\n"); break; default: std::printf("?\n"); }
    return 0;
}
