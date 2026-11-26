// R21: [over.ics.list] with [over.ics.rank]/3 - a braced argument among overloads that take different types.
#include <cstdio>
#include <initializer_list>
#include <string>
#include <vector>
int f(std::initializer_list<int>) { return 1; }
int f(int) { return 2; }
int g(std::initializer_list<double>) { return 1; }
int g(std::initializer_list<int>) { return 2; }
int h(std::vector<int>) { return 1; }
int h(int) { return 2; }
int k(std::string) { return 1; }
int k(const char *) { return 2; }
int m(long) { return 1; }
int m(std::initializer_list<long>) { return 2; }
int s(double) { return 1; }
int s(std::string) { return 2; }
int e(int) { return 1; }
int e(std::vector<int>) { return 2; }
int main() {
    std::printf("%d %d %d %d %d %d %d %d %d %d %d %d %d\n", f({1}), f({1, 2}), f(1), g({1, 2}), g({1.5, 2.5}),
                h({1, 2}), h(1), k({"s"}), m({1}), m(1), s({2.5}), e({}), e({1, 2, 3}));
    return 0;
}