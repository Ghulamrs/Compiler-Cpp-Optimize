#include <cstdio>
struct K {
    int m = 3;
    int run() { int t = 4; auto l = [&, this]() { return m + t; }; return l(); }
};
int main() {
    int a = 1, b = 2, c = 10;
    auto l1 = [=, &b]() mutable { b = a + b + c; a = 100; return a; };
    int r1 = l1();
    auto l2 = [&, a, c]() { b += a + c; };
    l2();
    K k;
    std::printf("%d %d %d %d\n", r1, a, b, k.run());
    return 0;
}
