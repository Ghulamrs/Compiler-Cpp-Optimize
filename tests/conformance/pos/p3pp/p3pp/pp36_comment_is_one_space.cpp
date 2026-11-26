#include <cstdio>
#define S(x) #x
#define X(x) S(x)
#define E
#define CAT(a, b) a ## b
int main() {
    std::printf("[%s] [%s] [%s] [%s] [%s]\n", S(a/**/b), S(a/* x */  /**/b), S( /*c*/ ),
                X(E/**/E), S(1 // gone
                             + 2));
    int CAT(v, /* between */ w) = 3;
    std::printf("%d\n", vw);
    return 0;
}
