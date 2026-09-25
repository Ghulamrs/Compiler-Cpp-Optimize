// [conv.qual] into an array element: `char (*)[3]` converts to
// `const char (*)[3]` as `char **` does to `const char *const *` - an
// array's cv-qualification is its element's (the cl review's A19).
extern "C" int printf(const char *, ...);
int total(const int (*rows)[2], int n) { int s = 0; for (int i = 0; i < n; ++i) s += rows[i][0] + rows[i][1]; return s; }
int main() {
    char grid[2][3] = {"ab", "cd"};
    const char (*g)[3] = grid;
    char (*h)[3] = grid;
    const char *const *q = 0;
    char **r = 0;
    q = r;
    int m[2][2] = {{1, 2}, {3, 4}};
    printf("%c %c %d %d\n", g[1][0], h[0][1], q == 0, total(m, 2));
    return 0;
}
