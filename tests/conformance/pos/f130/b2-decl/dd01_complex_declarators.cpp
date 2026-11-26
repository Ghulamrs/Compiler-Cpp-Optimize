#include <cstdio>
int twice(int x) { return x * 2; } int (*getf())(int) { return twice; } int (&getr())(int) { return twice; } int arr[2][3] = {{1, 2, 3}, {4, 5, 6}}; int (*parr())[3] { return arr; } int (&rarr())[2][3] { return arr; }
int main() { int (*fp)(int) = getf(); int (*(*pfp)())(int) = getf; int (*pa)[3] = parr(); int (&ra)[2][3] = rarr(); int (*fa[2])(int) = {twice, twice}; const int *const *cpp = nullptr;
std::printf("%d %d %d %d %d %d %d\n", fp(1), pfp()(2), getr()(3), pa[1][2], ra[0][1], fa[1](4), (int)(cpp == nullptr)); return 0; }
