// [dcl.enum]: scoped enums, underlying types, no implicit conversion, explicit casts, opaque declarations, sizeof, enumerator values with underlying type, enum in switch.
#include <cstdio>
enum class Color { Red, Green = 5, Blue };
enum class Small : unsigned char { A = 255 };
enum class Big : long long { X = 1LL << 40 };
enum Plain : short { P1 = -1, P2 };
enum Fwd : int;
enum Fwd : int { F1 = 3 };
enum class CFwd;
enum class CFwd { C1 = 9 };
struct Holder { enum class Inner { I = 2 }; };
int main() {
    Color c = Color::Blue;
    std::printf("%d %d %d\n", (int)c, (int)Color::Green, (int)sizeof(Color));
    std::printf("%d %d %d\n", (int)Small::A, (int)sizeof(Small), (int)sizeof(Big));
    std::printf("%lld\n", (long long)Big::X);
    std::printf("%d %d %d\n", P1, P2, (int)sizeof(Plain));
    std::printf("%d %d %d\n", F1, (int)CFwd::C1, (int)Holder::Inner::I);
    switch (c) { case Color::Red: std::printf("red\n"); break; case Color::Blue: std::printf("blue\n"); break; default: std::printf("other\n"); }
    Color d = static_cast<Color>(5);
    std::printf("%d %d %d\n", d == Color::Green, c > d, c != d);
    int v = static_cast<int>(c) + 1;
    std::printf("%d\n", v);
    unsigned char uc = static_cast<unsigned char>(Small::A);
    std::printf("%d\n", uc);
    enum class Empty : int {};
    std::printf("%d\n", (int)sizeof(Empty));
    return 0;
}
