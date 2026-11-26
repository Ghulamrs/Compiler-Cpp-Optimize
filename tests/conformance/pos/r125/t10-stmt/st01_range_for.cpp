// [stmt.ranged]: over arrays, containers, initializer_list, with auto&, const auto&, modifying through reference, member begin/end, ADL begin/end, temporaries' lifetime, nested, break/continue.
#include <cstdio>
#include <vector>
#include <initializer_list>
#include <string>
struct Range { int a[3]; const int *begin() const { return a; } const int *end() const { return a + 3; } };
namespace NS { struct Adl { int v[2]; }; const int *begin(const Adl &a) { return a.v; } const int *end(const Adl &a) { return a.v + 2; } }
std::vector<int> makeVec() { std::printf("make "); return std::vector<int>{1, 2, 3}; }
struct Tracked { Tracked() { a[0] = 7; a[1] = 8; std::printf("+ "); } ~Tracked() { std::printf("- "); } int a[2]; const int *begin() const { return a; } const int *end() const { return a + 2; } };
int main() {
    int arr[4] = {1, 2, 3, 4}; int sum = 0;
    for (int x : arr) sum += x;
    for (auto &x : arr) x *= 2;
    for (const auto &x : arr) sum += x;
    std::printf("%d\n", sum);
    std::vector<std::string> vs = {"a", "bb", "ccc"}; int len = 0; for (const std::string &s : vs) len += (int)s.size(); std::printf("%d\n", len);
    for (int x : {10, 20, 30}) std::printf("%d ", x); std::printf("\n");
    Range r = {{4, 5, 6}}; for (int x : r) std::printf("%d ", x); std::printf("\n");
    NS::Adl a = {{8, 9}}; for (int x : a) std::printf("%d ", x); std::printf("\n");
    for (int x : makeVec()) std::printf("%d ", x); std::printf("\n");
    for (int x : Tracked()) std::printf("%d ", x); std::printf("\n");
    int grid[2][3] = {{1, 2, 3}, {4, 5, 6}}; int t = 0; for (auto &row : grid) for (int x : row) t += x; std::printf("%d\n", t);
    int seen = 0; for (int x : arr) { if (x == 4) continue; if (x == 8) break; seen += x; } std::printf("%d\n", seen);
    std::vector<int> empty; int cnt = 0; for (int x : empty) ++cnt; std::printf("%d\n", cnt);
    char str[] = "hi"; int chars = 0; for (char c : str) if (c) ++chars; std::printf("%d\n", chars);
    return 0;
}
