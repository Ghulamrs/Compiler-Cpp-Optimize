// A byte loop whose return values are worked out on the way out: the second step reads what the first made, and what
// stays on in the loop keeps the old value of the same register.
#include <cstdio>
#include <locale>
template <class C> int order(const C *l, const C *le, const C *h, const C *he) {
    for (; l != le && h != he; ++l, ++h) {
        if (*l < *h) return -1;
        if (*h < *l) return 1;
    }
    if (h != he) return -1;
    if (l != le) return 1;
    return 0;
}
int minusNext(const char *p, const char *e) {
    for (; p != e; ++p) {
        if (*p == 'x') return -(*p + 1);
    }
    return 0;
}
int twice(const char *p, const char *e, int seen) {
    for (; p != e; ++p) {
        if (*p == 'y') return (seen + 1) * 2;
        seen = *p;
    }
    return seen;
}
int lengthOr(const char *s) {
    const char *p = s;
    while (*p) {
        if (*p == '!') return -static_cast<int>(p - s) - 1;
        ++p;
    }
    return static_cast<int>(p - s);
}
int main() {
    const char a[] = "apple", b[] = "apricot", c[] = "axle";
    std::printf("%d %d %d\n", order<char>(a, a + 5, b, b + 7), order<char>(b, b + 7, a, a + 5), order<char>(a, a + 5, a, a + 5));
    std::printf("%d %d\n", order<char>(a, a + 2, a, a + 5), order<char>(a, a + 5, a, a + 2));
    std::printf("%d %d\n", minusNext(c, c + 4), minusNext(a, a + 5));
    std::printf("%d %d\n", twice("azy", 3 + "azy", 5), twice("az", 2 + "az", 5));
    std::printf("%d %d %d\n", lengthOr("abc"), lengthOr("ab!c"), lengthOr(""));
    const std::collate<char> &co = std::use_facet<std::collate<char> >(std::locale::classic());
    std::printf("%d %d\n", co.compare(a, a + 5, b, b + 7), co.compare(b, b + 7, a, a + 5));
    return 0;
}
