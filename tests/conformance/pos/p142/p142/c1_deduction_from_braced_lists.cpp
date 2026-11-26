// [temp.deduct.call]/1 and CWG 1591: a braced list deduces initializer_list<P'>'s P' from its elements, and
// an array reference's element type and bound; anything else is a non-deduced context.
#include <cstdio>
#include <initializer_list>
#include <string>
template <class T> int il(std::initializer_list<T> l) { return (int)(l.size() * 10 + sizeof(T)); }
template <class T> T first(std::initializer_list<T> l, T fallback) { return l.size() ? *l.begin() : fallback; }
template <class T, int N> int cnt(T (&&)[N]) { return N; }
template <class T, int N> int cnt(T (&)[N]) { return -N; }
template <class T, int N> int sum(const T (&a)[N]) { int s = 0; for (int i = 0; i < N; i++) s += (int)a[i]; return s; }
template <class T> std::size_t chars(std::initializer_list<T> l) { std::size_t n = 0; for (const T &s : l) n += s.size(); return n; }
int main() {
    int a[2] = {5, 6};
    std::printf("%d %d %d %g %d %d %d %d %d\n", il({1, 2}), il({1.5, 2.5, 3.5}), il<char>({'a'}), first({2.5, 4.0}, 9.0),
                cnt({1, 2, 3, 4}), cnt(a), sum({1, 2, 3}), sum(a), (int)chars({std::string("ab"), std::string("cde")}));
    return 0;
}