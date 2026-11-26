#include <cstdio>
#include <functional>
int f(int a, int b, int c, int d, int e, int f, int g, int h, int i, int j) { return a * 1 + b * 2 + c * 3 + d * 4 + e * 5 + f * 6 + g * 7 + h * 8 + i * 9 + j * 10; }
int main() {
    using namespace std::placeholders;
    std::function<int(int, int, int, int, int, int, int, int, int, int)> b = std::bind(f, _10, _9, _8, _7, _6, _5, _4, _3, _2, _1);
    std::printf("%d\n", b(1, 2, 3, 4, 5, 6, 7, 8, 9, 10));
    return 0;
}
