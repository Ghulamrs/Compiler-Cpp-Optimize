// [expr.add], [expr.rel]: pointer arithmetic and difference, comparison, one-past-end, pointer to array element, subscripting with negative index, void* comparisons, pointer to member of array, nested arrays decay.
#include <cstdio>
#include <cstddef>
int main() {
    int arr[5] = {10, 20, 30, 40, 50};
    int *p = arr + 1, *q = &arr[4];
    std::printf("%d %d %d %d\n", (int)(q - p), *(p + 2), p[-1], 2[arr]);
    std::printf("%d %d %d\n", p < q, q == arr + 4, p + 4 == arr + 5);
    int (*pa)[5] = &arr; std::printf("%d %d\n", (*pa)[2], (int)sizeof(*pa));
    int m[2][3] = {{1, 2, 3}, {4, 5, 6}}; int (*row)[3] = m; std::printf("%d %d %d\n", row[1][2], (*(row + 1))[0], (int)(sizeof(m) / sizeof(m[0])));
    int *flat = &m[0][0]; std::printf("%d\n", flat[4]);
    const char *s = "hello"; std::printf("%c %d\n", *(s + 1), (int)(s + 5 - s));
    void *v = arr; std::printf("%d %d\n", v == arr, (int *)v == arr);
    char *c = (char *)arr; std::printf("%d\n", (int)((char *)(arr + 1) - c));
    std::ptrdiff_t diff = q - arr; std::size_t sz = sizeof(diff); std::printf("%d %d\n", (int)diff, (int)sz);
    int *np = 0; std::printf("%d %d\n", np == nullptr, !np);
    p += 2; p -= 1; ++p; --p; std::printf("%d\n", *p);
    return 0;
}
