// [stmt.goto]: a goto leaving two handlers at once, and one staying inside the outer
#include <cstdio>
struct T { int i; T(int i) : i(i) {} ~T() { std::printf("~%d ", i); } };
int main() {
    int n = 0;
again:
    n++;
    try { throw n; }
    catch (int e) {
        T t(e);
        try { throw e; }
        catch (int k) {
            if (k < 3) goto again;
            if (k == 3) goto inside;
        }
        std::printf("never ");
    inside:
        std::printf("inside%d ", e);
    }
    std::printf("n%d\n", n);
    return 0;
}
