// [stmt.iter]: a loop on a constant condition left by a break, in each spelling, the break before and after the work.
#include <cstdio>
int untilFive() {
    int w = 0;
    while (true) {
        if (++w > 4) break;
    }
    return w;
}
int byTwos(int n) {
    int i = 0;
    for (;;) {
        i += 2;
        if (i >= n) break;
    }
    return i;
}
int counted(int n) {
    int i = 0;
    do {
        if (++i == n) break;
    } while (1);
    return i;
}
int workAfter(int n) {
    int s = 0;
    int i = 0;
    while (1) {
        if (i == n) break;
        s += i;
        i++;
    }
    return s;
}
int twoWays(int n) {
    int s = 0;
    int i = 0;
    while (true) {
        if (i == n) break;
        s += i;
        i++;
        if (s > 20) break;
    }
    return s * 100 + i;
}
int nested(int n) {
    int outer = 0;
    int total = 0;
    while (true) {
        int inner = 0;
        while (true) {
            total += inner;
            if (++inner >= 3) break;
        }
        if (++outer == n) break;
    }
    return total;
}
int main() {
    std::printf("%d\n", untilFive());
    std::printf("%d %d\n", byTwos(7), byTwos(8));
    std::printf("%d %d\n", counted(1), counted(9));
    std::printf("%d %d\n", workAfter(0), workAfter(6));
    std::printf("%d %d\n", twoWays(3), twoWays(50));
    std::printf("%d\n", nested(4));
    return 0;
}
