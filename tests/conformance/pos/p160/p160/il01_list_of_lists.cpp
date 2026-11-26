// [dcl.init.list]/3: an element of an initializer_list that is itself one is made from its braces the same way -
// of two items too, where a constructor of two arguments might have been looked for.
#include <cstdio>
#include <initializer_list>
#include <vector>
struct Mat {
    int rows, total;
    Mat(std::initializer_list<std::initializer_list<int> > r) : rows((int)r.size()), total(0) {
        for (const std::initializer_list<int> &row : r)
            for (int x : row) total += x;
    }
};
int main() {
    Mat a = {{1, 2}, {3, 4}, {5}};
    Mat b{{1}, {}, {2, 3, 4}};
    std::printf("%d %d %d %d\n", a.rows, a.total, b.rows, b.total);
    std::initializer_list<std::initializer_list<int> > n = {{7, 8}, {9, 10}};
    std::printf("%d %d\n", (int)n.size(), (int)n.begin()->size());
    std::vector<std::vector<int> > v = {{1, 2}, {3}};
    std::printf("%d %d\n", (int)v.size(), v[0][1]);
    return 0;
}
