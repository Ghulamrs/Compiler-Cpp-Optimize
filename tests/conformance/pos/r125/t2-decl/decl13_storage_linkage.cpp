// [basic.link], [basic.stc]: static locals initialised once, extern const, const at namespace scope has internal linkage, static functions, inline functions with static locals share one copy, global init order within a TU.
#include <cstdio>
const int cval = 10;             // internal linkage
extern const int eval = 11;      // external
static int sfun() { return 12; }
inline int counter() { static int n = 0; return ++n; }
int first = 1;
int second = first + 1;
struct Init { Init(const char *s) { std::printf("init %s\n", s); } ~Init() { std::printf("dtor\n"); } };
Init g1("g1"); Init g2("g2");
int once() { static int calls = 0; static Init local("local"); return ++calls; }
int main() {
    std::printf("%d %d %d %d\n", cval, eval, sfun(), second);
    int c1 = counter(), c2 = counter(), c3 = counter(); std::printf("%d %d %d\n", c1, c2, c3);
    int o1 = once(); int o2 = once(); std::printf("%d %d\n", o1, o2);
    static const double sd = 2.5; std::printf("%g\n", sd);
    { static int inner = 5; inner++; std::printf("%d\n", inner); }
    std::printf("end main\n");
    return 0;
}
