// A braced list handed to a `std::initializer_list<T>` parameter - the
// backing array is built in the caller's frame, as the declaration form's is,
// and the list points into it for the length of the full expression.
#include <initializer_list>
extern "C" int printf(const char *, ...);
extern "C" unsigned long strlen(const char *);

static int sum(std::initializer_list<int> xs) {
    int s = 0;
    for (const int *p = xs.begin(); p != xs.end(); ++p) s += *p;
    return s;
}

static double mean(std::initializer_list<double> xs, int scale) {
    double s = 0;
    for (const double *p = xs.begin(); p != xs.end(); ++p) s += *p;
    return s * scale / static_cast<double>(xs.size());
}

struct Acc {
    int total;
    Acc() : total(0) {}
    void add(std::initializer_list<int> xs) {
        for (const int *p = xs.begin(); p != xs.end(); ++p) total += *p;
    }
};

static int count(std::initializer_list<const char *> names) {
    int n = 0;
    for (const char *const *p = names.begin(); p != names.end(); ++p) n += static_cast<int>(strlen(*p));
    return n;
}

int main() {
    printf("sum %d %d %d\n", sum({1, 2, 3}), sum({}), sum({40}));
    printf("mean %g\n", mean({1.0, 2.0, 3.0, 4.0}, 2));
    Acc a;
    a.add({5, 6});
    a.add({7});
    printf("member %d\n", a.total);
    printf("strings %d\n", count({"ab", "cde", "f"}));
    return 0;
}
