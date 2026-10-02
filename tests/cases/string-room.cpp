// What the refusal of `char s[3] = "abc"` must leave alone: an array exactly
// one longer than the string, one with its length taken from the string, one
// longer still (zero-filled), and each of those at file scope and as a member.
#include <stdio.h>

struct Name { char text[4]; int n; };
char g1[4] = "abc";
char g2[] = "abcd";
char g3[8] = "ab";
Name gn = { "xyz", 7 };

int main(void) {
    char a[4] = "abc";
    char b[] = "hello";
    char c[6] = "hi";
    Name ln = { "pqr", 3 };
    printf("%s %s %s %s %d\n", g1, g2, g3, gn.text, gn.n);
    printf("%s %s %s %s %d\n", a, b, c, ln.text, ln.n);
    printf("%d %d %d %d\n", (int)sizeof g2, (int)sizeof b, c[2], c[5]);
    return 0;
}
