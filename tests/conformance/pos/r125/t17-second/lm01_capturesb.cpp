// [expr.prim.lambda]: capture by copy, by reference, default captures, mixed, this, mutable, captured copies made at creation time, reference captures see later changes, nested lambdas.
#include <cstdio>
struct Obj {
    int v = 10;
    int viaThis() { auto l = [this] { return v * 2; }; return l(); }
    int viaDefault() { auto l = [=] { return v + 1; }; v = 99; return l(); }
    int modify() { auto l = [this] { v += 5; }; l(); return v; }
};
int main() {
    int a = 1, b = 2;
    auto byCopy = [a] { return a; };
    auto byRef = [&a] { return a; };
    auto all = [=] { return a + b; };
    auto allRef = [&] { a++; b++; return a + b; };
    auto mixed = [a, &b] { b += 10; return a + b; };
    a = 5;
    std::printf("%d %d %d\n", byCopy(), byRef(), all());
    int r1 = allRef(); int r2 = mixed(); std::printf("%d %d %d %d\n", r1, r2, a, b);
    auto mut = [a]() mutable { a += 100; return a; };
    int m1 = mut(); int m2 = mut(); std::printf("%d %d %d\n", m1, m2, a);
    Obj o; int t1 = o.viaThis(); int t2 = o.viaDefault(); int t3 = o.modify(); std::printf("%d %d %d\n", t1, t2, t3);
    auto outer = [a](int k) { auto inner = [a, k](int m) { return a + k + m; }; return inner(1); };
    std::printf("%d\n", outer(2));
    auto gen = [](int x) { return [x](int y) { return x * y; }; };
    std::printf("%d\n", gen(6)(7));
    int arr[3] = {1, 2, 3};
    auto arrCopy = [arr] { return arr[0] + arr[2]; };
    arr[0] = 100;
    std::printf("%d\n", arrCopy());
    return 0;
}
