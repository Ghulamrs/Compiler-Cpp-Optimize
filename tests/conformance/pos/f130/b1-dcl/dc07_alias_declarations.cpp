#include <cstdio>
#include <type_traits>
using I = int; using FP = int (*)(int); using AR = int[3]; template <class T> using Ptr = T*; template <class T, int N> using Arr = T[N]; template <class T> using Id = T;
struct S { using Inner = double; template <class T> using M = T; }; int twice(int x) { return 2 * x; }
int main() { I i = 1; FP f = twice; AR a = {1, 2, 3}; Ptr<int> p = &i; Arr<char, 2> c = {'a', 'b'}; S::Inner d = 1.5; S::M<int> m = 4; Id<const int> ci = 5; using L = I; L l = 6;
std::printf("%d %d %d %d %c %g %d %d %d %d\n", i, f(2), a[2], *p, c[1], d, m, ci, l, (int)std::is_same<Ptr<Ptr<int>>, int**>::value); return 0; }
