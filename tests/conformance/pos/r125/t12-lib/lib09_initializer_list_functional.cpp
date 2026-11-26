// <initializer_list> by itself (size, begin/end, in constructors, as a function parameter, nested), <functional> function objects (less, greater, plus) with sort and templates; <array>.
#include <cstdio>
#include <initializer_list>
#include <functional>
#include <algorithm>
#include <array>
int sum(std::initializer_list<int> l) { int s = 0; for (const int *p = l.begin(); p != l.end(); ++p) s += *p; return s; }
struct Mat { int rows; int total; Mat(std::initializer_list<std::initializer_list<int> > rows) : rows((int)rows.size()), total(0) { for (const std::initializer_list<int> &r : rows) total += sum(r); } };
template <class Op> int fold(std::initializer_list<int> l, Op op, int init) { for (int x : l) init = op(init, x); return init; }
int main() {
    std::printf("%d %d %d\n", sum({1, 2, 3}), sum({}), (int)std::initializer_list<int>{4, 5}.size());
    Mat m = {{1, 2}, {3, 4}, {5}}; std::printf("%d %d\n", m.rows, m.total);
    std::printf("%d %d %d\n", fold({1, 2, 3, 4}, std::plus<int>(), 0), fold({1, 2, 3, 4}, std::multiplies<int>(), 1), fold({5, 3, 8}, [](int a, int b) { return a > b ? a : b; }, 0));
    int arr[] = {3, 1, 2}; std::sort(arr, arr + 3, std::greater<int>()); std::printf("%d %d %d\n", arr[0], arr[1], arr[2]);
    std::printf("%d %d %d\n", std::less<int>()(1, 2), std::equal_to<int>()(2, 2), std::negate<int>()(5));
    std::array<int, 4> a = {{4, 3, 2, 1}}; std::sort(a.begin(), a.end()); std::printf("%d %d %d %d\n", a[0], a[3], (int)a.size(), a.front());
    std::array<double, 2> da = {{1.5, 2.5}}; double t = 0; for (double x : da) t += x; std::printf("%g\n", t);
    return 0;
}
