#include <cstdio>
#define S(x) #x
#define X(x) S(x)
#define CAT(a, b) a ## b
int main() {
    std::printf("%s %s %s %s %s\n", S(0xe+1), S(1.2.3), S(.5e-2_x), X(CAT(1, e+5)), S(1..e+-));
    return 0;
}
