#include <cstdio>
#include <initializer_list>
#include <string>
static int sum(std::initializer_list<int> il) {
    int s = 0;
    for (const int *p = std::begin(il); p != std::end(il); ++p) s += *p;
    return s;
}
int main() {
    std::initializer_list<int> empty;
    std::initializer_list<double> d = { 1.5, 2.5 };
    std::printf("%d %d %d %g %d\n", sum({ 1, 2, 3, 4 }), sum({}), (int)(std::end(empty) - std::begin(empty)), *(std::end(d) - 1), (int)(std::end(d) - std::begin(d)));
    return 0;
}
