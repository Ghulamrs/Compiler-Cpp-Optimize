// [dcl.init.aggr]/11: an expression that converts to an aggregate member's type initialises the member, and
// braces are elided only where it does not - `W<G> w{g}` copies g, `{1, 2}` fills G's members.
#include <cstdio>
#include <initializer_list>
struct G { int x, y, z; };
struct Dg : G {};
template <class T> struct W { T t; };
struct Two { G a; G b; };
struct K { int k; char t; };
template <class T> int sum(std::initializer_list<T> l) { int s = 0; for (const T &e : l) s += e.k; return s; }
int main() {
    G g = {1, 2, 3};
    W<G> w{g};
    W<G> v = {g};
    W<G> e = {4, 5, 6};
    Two two = {g, {7, 8, 9}};
    Two flat = {1, 2, 3, 4, 5, 6};
    K a = {1, 'x'}, b = {2, 'y'};
    std::initializer_list<K> x = {a, b};
    K arr[2] = {a, b};
    std::printf("%d %d %d %d %d %d\n", w.t.z, v.t.y, e.t.x, two.a.z, two.b.x, flat.b.z);
    std::printf("%d %d %d %c\n", (int)x.size(), sum({a, b}), arr[1].k, arr[0].t);
    return 0;
}
