#include <cstdio>
int main() {
    int r = 0;
    [[]] r = 1;
    [[]] if (r == 1) r = 2;
    [[]] for (int i = 0; i < 3; i++) r += i;
    [[]] { r *= 2; }
    [[]] [[]] while (r > 100) r--;
    std::printf("%d\n", r);
    [[]] return 0;
}
