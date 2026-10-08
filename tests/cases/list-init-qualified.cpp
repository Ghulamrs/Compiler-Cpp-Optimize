// `T{...}` as an expression under every spelling of T: a class in a
// namespace, a class template-id plain and in a namespace, a typedef, and a
// nested class - [expr.type.conv]/2 for each.
extern "C" int printf(const char *, ...);
namespace n {
    struct S { int a, b; };
    template <class T> struct C { T a; T b; };
    template <class T> struct D { T a; D(T x) : a(x) {} };
}
template <class T> struct P { T a; T b; };
template <class T> struct Q { T a; Q(T x) : a(x) {} };
typedef n::S TS;
struct M { struct In { int z; }; };
int main() {
    int a = n::S{1, 2}.b;
    int b = P<int>{3, 4}.b;
    int c = Q<int>{5}.a;
    int d = TS{6, 7}.a;
    int e = M::In{8}.z;
    int f = n::C<int>{9, 10}.b;
    int g = n::D<int>{11}.a;
    printf("%d %d %d %d %d %d %d\n", a, b, c, d, e, f, g);
    return 0;
}
