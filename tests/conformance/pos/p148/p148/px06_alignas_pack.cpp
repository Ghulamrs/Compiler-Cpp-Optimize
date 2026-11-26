// [dcl.align]/4: alignas with a pack expansion is one alignas per member, the strictest taking effect.
#include <cstdio>
template <class... Ts> struct ByType { alignas(Ts...) char c; };
template <class... Ts> struct alignas(Ts...) Whole { char c; };
struct alignas(16) Wide { char c; };
int main() {
    std::printf("%d %d %d\n", (int)alignof(ByType<char, double, short>), (int)alignof(ByType<char>),
                (int)alignof(ByType<Wide, int>));
    std::printf("%d %d\n", (int)alignof(Whole<int, char>), (int)sizeof(Whole<Wide>));
    return 0;
}
