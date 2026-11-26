// Constant expressions over constexpr arrays, string literals and pointers ([expr.const]/2-3, [dcl.constexpr]/9):
// subscripts, pointers into an array passed to and returned by constexpr functions, a constexpr pointer read
// through, recursion over a string literal, and an lvalue `?:` choosing what is read.
#include <cstdio>

constexpr int table[] = {2, 3, 5, 7, 11};
constexpr int count = sizeof table / sizeof table[0];
static_assert(table[3] == 7, "subscript");

constexpr int sum(const int *p, int n) { return n == 0 ? 0 : *p + sum(p + 1, n - 1); }
constexpr int length(const char *s) { return *s ? 1 + length(s + 1) : 0; }
constexpr int digits(const char *s, int acc) { return *s ? digits(s + 1, acc * 10 + (*s - '0')) : acc; }
constexpr const int *larger(const int *a, const int *b) { return *a > *b ? a : b; }
constexpr int pick(int a, int b, bool first) { return first ? a : b; }
constexpr int maxOf(int a, int b) { return a > b ? a : b; }

constexpr const int *third = table + 2;
constexpr const int *big = larger(table + 1, table + 3);
constexpr const char *word = "constant";
static_assert(sum(table, count) == 28, "sum");
static_assert(length("abcd") == 4 && length(word) == 8, "length");
static_assert(digits("1234", 0) == 1234, "digits");
static_assert(*third == 5 && *big == 7 && third[1] == 7, "pointers");
static_assert(word[2] == 'n', "word");
static_assert(pick(4, 9, false) == 9 && maxOf(maxOf(1, 6), 3) == 6, "lvalue ?:");

struct Weights {
    static constexpr int w[3] = {10, 20, 30};
    static constexpr const char *name = "weights";
};
constexpr int Weights::w[3];
constexpr const char *Weights::name;
static_assert(Weights::w[1] == 20 && length(Weights::name) == 7, "members");

int main() {
    constexpr int local[] = {4, 1, 6};
    static_assert(local[2] == 6 && sum(local, 3) == 11, "local array");
    constexpr const char *greeting = "hi there";
    static_assert(greeting[3] == 't', "local pointer");
    int bounds[sum(table, 3)];
    std::printf("%d %d %d %d %d %d %s %d %s\n", count, sum(table, count), *third, *big, length(word),
                static_cast<int>(sizeof bounds / sizeof bounds[0]), greeting, Weights::w[2], Weights::name);
    return 0;
}
