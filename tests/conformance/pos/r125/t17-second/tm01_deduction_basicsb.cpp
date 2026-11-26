// [temp.deduct.call]: array-to-pointer and function-to-pointer decay for by-value params, no decay for references; array size deduction; cv dropping; deduction from nested template-ids; non-deduced contexts with explicit args.
#include <cstdio>
template <class T> int byVal(T) { return (int)sizeof(T); }
template <class T> int byRef(T &) { return (int)sizeof(T); }
template <class T, int N> int arrSize(T (&)[N]) { return N; }
template <class T> const char *ptrOrNot(T *) { return "ptr"; }
template <class T> const char *ptrOrNot(T) { return "val"; }
template <class T> struct Box { T v; };
template <class T> T unbox(const Box<T> &b) { return b.v; }
template <class T> struct Id { typedef T type; };
template <class T> T nonDeduced(typename Id<T>::type v) { return v + 1; }
template <class R, class A> R callIt(R (*f)(A), A a) { return f(a); }
int dbl(int x) { return x * 2; }
template <class T, class U> struct Pair { T a; U b; };
template <class T, class U> int sum(const Pair<T, U> &p) { return (int)(p.a + p.b); }
int main() {
    int arr[7]; const int ci = 1; const char *s = "abc";
    std::printf("%d %d\n", byVal(arr), arrSize(arr));
    std::printf("%d %d %d\n", byVal(ci), byRef(ci), byVal("hello"));
    std::printf("%s %s %s\n", ptrOrNot(arr), ptrOrNot(s), ptrOrNot(ci));
    Box<double> b = {2.5}; std::printf("%g\n", unbox(b));
    std::printf("%d %d\n", nonDeduced<int>(4), nonDeduced<long>(5L) == 6);
    std::printf("%d\n", callIt(dbl, 21));
    Pair<int, double> p = {1, 2.5}; std::printf("%d\n", sum(p));
    std::printf("%d\n", byVal(dbl) == (int)sizeof(void *));
    return 0;
}
