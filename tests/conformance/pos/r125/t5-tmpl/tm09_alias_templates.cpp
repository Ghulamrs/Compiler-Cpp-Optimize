// [temp.alias]: alias templates as parameters, in deduction (an alias is transparent), nested alias, alias to member typedef, alias with non-type param, alias of function type.
#include <cstdio>
#include <vector>
#include <type_traits>
template <class T> using Vec = std::vector<T>;
template <class T> using Ptr = T *;
template <class T> using CRef = const T &;
template <int N> using IntArr = int[N];
template <class T> using Fn = T (*)(T);
template <class T> using RemCRef = typename std::remove_const<typename std::remove_reference<T>::type>::type;
template <class T> int vsize(const Vec<T> &v) { return (int)v.size(); }
template <class T> const char *kind(Ptr<T>) { return "ptr"; }
template <class A, class B> struct Same { static const bool v = false; };
template <class A> struct Same<A, A> { static const bool v = true; };
template <class T> struct Wrap { template <class U> using Rebind = Wrap<U>; typedef T type; };
int dbl(int x) { return x * 2; }
int main() {
    Vec<int> v = {1, 2, 3}; std::printf("%d\n", vsize(v));
    int i = 0; Ptr<int> p = &i; std::printf("%s\n", kind(p));
    CRef<int> r = i; (void)r; IntArr<3> arr = {1, 2, 3}; std::printf("%d\n", arr[2]);
    Fn<int> f = dbl; std::printf("%d\n", f(4));
    std::printf("%d %d\n", Same<RemCRef<const int &>, int>::v, Same<Vec<int>, std::vector<int> >::v);
    Wrap<int>::Rebind<double>::type d = 1.5; std::printf("%g\n", d);
    std::printf("%d\n", Same<Wrap<char>::Rebind<long>, Wrap<long> >::v);
    return 0;
}
