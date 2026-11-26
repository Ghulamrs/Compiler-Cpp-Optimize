// [expr.new]/2 `new auto(e)`, and /6 `new T[n][3]`.
#include <cstdio>
int main(int argc, char **) {
    auto *i = new auto(9);
    auto *d = new auto(2.5);
    int n = argc + 1;
    int (*m)[3] = new int[n][3];
    double (*z)[2][2] = new double[2][2][2]();
    m[1][2] = 7;
    std::printf("%d %g %d %g %d\n", *i, *d, m[1][2], z[1][1][1], (int)sizeof(*m));
    delete i; delete d; delete[] m; delete[] z;
    return 0;
}