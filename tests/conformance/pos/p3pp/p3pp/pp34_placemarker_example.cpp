#include <cstdio>
#define t(x,y,z) x ## y ## z
int j[] = { t(1,2,3), t(,4,5), t(6,,7), t(8,9,),
    t(10,,), t(,11,), t(,,12), t(,,) };
#define OBJ_LIKE (1-1)
#define OBJ_LIKE /* white space */ (1-1) /* other */
#define FUNC_LIKE(a) ( a )
#define FUNC_LIKE( a )( /* note the white space */ \
    a /* other stuff on this line
    */ )
#define hash_hash # ## #
#define mkstr(a) # a
#define in_between(a) mkstr(a)
#define join(c, d) in_between(c hash_hash d)
char p[] = join(x, y);
int main() {
    for (unsigned k = 0; k < sizeof j / sizeof j[0]; k++) std::printf("%d ", j[k]);
    std::printf("%d %d [%s]\n", OBJ_LIKE, FUNC_LIKE(4), p);
    return 0;
}
