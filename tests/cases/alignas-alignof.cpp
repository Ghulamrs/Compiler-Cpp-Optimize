// **alignas on a class, a member, a global and a local; alignof of a type.**
// The locals stay within 8, the least any target keeps its stack to; a global
// or a member may ask for more, and the emulator's assembler had to learn to
// place a section at the alignment it asks for before the 64 here came out right.
extern "C" int printf(const char *, ...);
struct alignas(16) Al { char c; };
Al a;
struct M { char a; alignas(8) char b; char c; };
struct N { alignas(double) int i; char k; };
struct Weak { alignas(4) int x; };
alignas(32) char gbuf[10];
alignas(16) int gi = 5;
alignas(64) static double gd = 2.5;
int main() {
    alignas(8) char buf[3];
    alignas(8) int loc = 7;
    a.c = 'q';
    M m; m.a = 1; m.b = 2; m.c = 3;
    printf("%d %d %d %d %d\n", (int)sizeof(Al), (int)alignof(Al), (int)sizeof(M), (int)alignof(M), (int)sizeof(N));
    printf("%d %d %d %d\n", (int)alignof(N), (int)sizeof(Weak), (int)alignof(int), (int)alignof(char[7]));
    printf("%d %d %d %d\n", (int)((unsigned long long)&gbuf % 8), (int)((unsigned long long)&gi % 16),
           (int)((unsigned long long)&gd % 64), (int)((unsigned long long)buf % 8));
    printf("%d %d %d %d %d\n", (int)((unsigned long long)&loc % 8), (int)((unsigned long long)&a % 16),
           a.c, m.a + m.b + m.c, loc + gi);
    printf("%d %d\n", (int)alignof(Al), (int)alignof(double));
    return 0;
}
