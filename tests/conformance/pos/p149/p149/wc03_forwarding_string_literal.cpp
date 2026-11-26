// [temp.deduct.call]/3: a string literal bound to a forwarding reference is an lvalue `const char (&)[N]`,
// forwarded intact - to a pointer, into a std::string, through a pair-like converting constructor.
#include <cstdio>
#include <string>
#include <utility>
template <class U> const char *ptr(U &&u) { return u; }
template <class U> const char *fwd(U &&u) { return ptr(std::forward<U>(u)); }
template <class U> int len(U &&u) { return (int)sizeof(u); }
template <class A, class B> struct P {
    A a; B b;
    template <class U1, class U2> P(U1 &&x, U2 &&y) : a(std::forward<U1>(x)), b(std::forward<U2>(y)) {}
};
template <class T, class U> T make(U &&u) { return T(std::forward<U>(u)); }
int main() {
    P<std::string, int> p("xyz", 1);
    P<const char *, int> q("lit", 2);
    std::string s = make<std::string>("made");
    std::printf("%s %s %d\n", ptr("abc"), fwd("hello"), len("xy"));
    std::printf("%s %d %s %d %s\n", p.a.c_str(), p.b, q.a, q.b, s.c_str());
    return 0;
}
