#include <cstdio>
#define F(x) (x * 2)
#define EMPTY
int F = 7;
int main() {
    int r = F
        (3);
    std::printf("%d %d %d\n", r, F, F EMPTY + 1);
    return 0;
}
