// Alternative tokens [lex.key]: and, or, not, bitand, compl, not_eq; digraphs <: :> <% %> %:
#include <cstdio>
int main() {
    int a = 6, b = 3;
    bool r1 = a and b, r2 = a or b, r3 = not a;
    int r4 = a bitand b, r5 = a bitor b, r6 = a xor b, r7 = compl a;
    bool r8 = a not_eq b;
    a and_eq 7; b or_eq 8;
    std::printf("%d %d %d %d %d %d %d %d %d %d\n", r1, r2, r3, r4, r5, r6, r7, r8, a, b);
    int arr<:3:> = <% 1, 2, 3 %>;
    std::printf("%d\n", arr<:2:>);
    %:define DIG 9
    std::printf("%d\n", DIG);
    return 0;
}
