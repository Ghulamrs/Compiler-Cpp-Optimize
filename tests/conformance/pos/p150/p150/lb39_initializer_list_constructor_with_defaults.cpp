// [dcl.init.list]/2 and [dcl.fct.default]: an initializer-list constructor with defaulted parameters, and a member template's default naming the class's parameter.
#include <cstdio>
#include <initializer_list>
struct Less { bool operator()(int a, int b) const { return a < b; } };
struct Alloc {};
template <class K, class V, class C = Less, class A = Alloc> struct Map {
    int n;
    struct value_type { K k; V v; };
    Map(std::initializer_list<value_type> l, const C & = C(), const A & = A()) : n((int)l.size()) {}
};
template <class T, class C = Less> struct Set {
    int n;
    template <class It> Set(It a, It b, const C &c = C()) : n((int)(b - a)) { (void)c; }
};
int main() {
    Map<int, char> m{{3, 'c'}, {4, 'd'}};
    int arr[3] = {1, 2, 3};
    Set<int> s(arr, arr + 3);
    std::printf("%d %d\n", m.n, s.n);
    return 0;
}
