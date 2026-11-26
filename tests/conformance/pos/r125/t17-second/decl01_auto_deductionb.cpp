// [dcl.spec.auto]: auto drops top-level const and references, auto& keeps, const auto&, auto* from pointer, auto with initializer_list.
#include <cstdio>
#include <initializer_list>
template <class T> struct Is { static const int same = 0; };
template <> struct Is<int> { static const int same = 1; };
template <class A, class B> struct Same { static const bool v = false; };
template <class A> struct Same<A, A> { static const bool v = true; };
template <class T> int kind(T &) { return 1; }
int main() {
    const int ci = 5;
    auto a = ci;              // int
    const auto &b = ci;       // const int&
    const auto c = ci;        // const int
    auto *p = &ci;            // const int*
    auto d = {1, 2, 3};       // std::initializer_list<int>
    auto e = 1.5f;            // float
    auto f = "str";           // const char*
    auto g = 1u + 2;          // unsigned
    auto h = 'c' + 1;         // int
    auto i = true;            // bool
    std::printf("%d %d %d %d %d\n", Same<decltype(a), int>::v, Same<decltype(b), const int &>::v, Same<decltype(c), const int>::v, Same<decltype(p), const int *>::v, Same<decltype(d), std::initializer_list<int> >::v);
    std::printf("%d %d %d %d %d\n", Same<decltype(e), float>::v, Same<decltype(f), const char *>::v, Same<decltype(g), unsigned>::v, Same<decltype(h), int>::v, Same<decltype(i), bool>::v);
    a = 6;  // a is a copy
    std::printf("%d %d %d\n", a, ci, (int)d.size());
    int arr[3] = {7, 8, 9};
    auto arr2 = arr;          // int*
    auto &arr3 = arr;         // int(&)[3]
    std::printf("%d %d %d\n", Same<decltype(arr2), int *>::v, Same<decltype(arr3), int(&)[3]>::v, (int)sizeof(arr3));
    auto fn = &kind<int>;
    std::printf("%d\n", fn(a));
    return 0;
}
