#include <cstdio>
namespace N { int f(int) { return 1; } struct T { int v; }; int swap(T &, T &) { return 7; } }
int f(double) { return 2; }
namespace M {
    int g(long) { return 3; }
    int run() {
        int a = f(1.5);
        {
            using N::f;
            a = a * 10 + f(1.5);
            {
                int f = 4;
                a = a * 10 + f;
            }
            a = a * 10 + f(2);
        }
        return a * 10 + f(1.5);
    }
}
namespace S { template <class T> int sw(T &, T &) { return 5; } }
int main() {
    std::printf("%d\n", M::run());
    N::T x = {1}, y = {2};
    { using S::sw; std::printf("%d\n", sw(x, y)); }
    { using std::printf; printf("%d\n", swap(x, y)); }
    return 0;
}
