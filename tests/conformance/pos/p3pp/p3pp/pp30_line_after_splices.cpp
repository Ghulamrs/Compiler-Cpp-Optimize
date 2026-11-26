#include <cstdio>
#define LONG_MACRO(a, b) \
    ((a) + \
     (b))
int main() {
    std::printf("%d %d\n", LONG_MACRO(1, 2), __LINE__);
    return 0;
}
