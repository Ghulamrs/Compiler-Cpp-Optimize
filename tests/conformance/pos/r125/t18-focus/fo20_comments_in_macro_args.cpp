// Comments are replaced by a space in translation phase 3, before macro expansion; they must not survive into a stringized argument.
#include <cstdio>
#include <cstring>
#define S(x) #x
#define SUM(a, b) ((a) + (b))
int main() {
    std::printf("[%s] %d\n", S(/* c */ z /* d */), (int)std::strlen(S(a /* comment */ b)));
    std::printf("%d\n", SUM(1 /* one */, /* two */ 2));
    return 0;
}
