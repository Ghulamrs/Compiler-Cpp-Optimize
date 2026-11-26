// [cpp.rescan]: an object-like macro expanding to a function-like macro's name is rescanned together with the following tokens of the source.
#include <cstdio>
#define F(x) ((x) * 2)
#define G F
#define H() G
#define ID(x) x
#define CALL(m, a) m(a)
int main() {
    std::printf("%d %d %d %d\n", G(3), H()(4), ID(F)(5), CALL(F, 6));
    return 0;
}
