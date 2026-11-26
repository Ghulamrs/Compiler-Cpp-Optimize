#include <cstdio>
#include <vector>
#include <initializer_list>
struct R { int a[3] = {1, 2, 3}; int *begin() { return a; } int *end() { return a + 3; } };
struct Q { int a[2] = {7, 8}; }; int *begin(Q &q) { return q.a; } int *end(Q &q) { return q.a + 2; }
std::vector<int> make() { std::printf("make "); return std::vector<int>{4, 5}; }
int main() { int arr[] = {1, 2}; for (int x : arr) std::printf("%d", x); for (int x : {9, 8}) std::printf("%d", x); R r; for (int &x : r) x *= 2; for (auto x : r) std::printf("%d", x); Q q; for (auto &&x : q) std::printf("%d", x);
for (int x : make()) std::printf("%d", x); const int ca[2] = {5, 6}; for (const auto &x : ca) std::printf("%d", x); std::printf("\n"); return 0; }
