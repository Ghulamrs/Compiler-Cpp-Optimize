// [dcl.type.simple]/4: decltype(id) is the declared type; decltype((lvalue)) is T&; prvalue -> T; xvalue -> T&&; function call -> return type.
#include <cstdio>
#include <utility>
template <class A, class B> struct Same { static const bool v = false; };
template <class A> struct Same<A, A> { static const bool v = true; };
struct S { int m; double d; };
int f();
int &g();
int &&h();
const int &k();
int main() {
    int x = 0; const int cx = 1; int &rx = x; int arr[2] = {0, 0}; S s = {1, 2.0}; S *ps = &s;
    std::printf("%d %d %d %d\n", Same<decltype(x), int>::v, Same<decltype((x)), int &>::v, Same<decltype(cx), const int>::v, Same<decltype((cx)), const int &>::v);
    std::printf("%d %d %d\n", Same<decltype(rx), int &>::v, Same<decltype((rx)), int &>::v, Same<decltype(arr), int[2]>::v);
    std::printf("%d %d %d %d\n", Same<decltype(f()), int>::v, Same<decltype(g()), int &>::v, Same<decltype(h()), int &&>::v, Same<decltype(k()), const int &>::v);
    std::printf("%d %d %d\n", Same<decltype(s.m), int>::v, Same<decltype((s.m)), int &>::v, Same<decltype(ps->d), double>::v);
    std::printf("%d %d %d\n", Same<decltype(x + 1), int>::v, Same<decltype(x = 1), int &>::v, Same<decltype(++x), int &>::v);
    std::printf("%d %d %d\n", Same<decltype(x++), int>::v, Same<decltype(std::move(x)), int &&>::v, Same<decltype(arr[0]), int &>::v);
    std::printf("%d %d %d\n", Same<decltype(1.0 + 1), double>::v, Same<decltype('a'), char>::v, Same<decltype("ab"), const char(&)[3]>::v);
    std::printf("%d %d\n", Same<decltype(x > 0 ? x : cx), const int &>::v, Same<decltype(true ? x : x), int &>::v);
    std::printf("%d %d\n", Same<decltype(*ps), S &>::v, Same<decltype(&s), S *>::v);
    std::printf("%d %d\n", Same<decltype(S()), S>::v, Same<decltype(nullptr), decltype(nullptr)>::v);
    decltype(x) y = 5; decltype((x)) z = x; z = 9;
    std::printf("%d %d\n", y, x);
    return 0;
}
