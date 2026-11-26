// Dead code elimination must keep observable effects: unused results of calls with side effects, assignments to volatile-free globals read later, empty loops with side-effect conditions, a call whose only effect is through a pointer.
#include <cstdio>
int g = 0;
int effect() { ++g; return 0; }
void write(int *p) { *p = 42; }
int readG() { return g; }
int pureish(int x) { return x * 2; }
int main() {
    effect(); (void)effect(); int unused = effect(); (void)unused;
    std::printf("%d\n", g);
    int k = 0; while (effect(), k++ < 3) {} std::printf("%d %d\n", g, k);
    int local = 0; write(&local); std::printf("%d\n", local);
    for (int i = 0; i < 1000; ++i) pureish(i); std::printf("%d\n", readG());
    int arr[10]; for (int i = 0; i < 10; ++i) arr[i] = i; int s = 0; for (int i = 0; i < 10; ++i) if (arr[i] % 2) s += arr[i]; std::printf("%d\n", s);
    if (g > 100) std::printf("never\n"); else if (g == 7) std::printf("seven\n"); else std::printf("other %d\n", g);
    int x = 5; x = x; x += 0; x *= 1; x = x + 0 * g; std::printf("%d\n", x);
    return 0;
}
