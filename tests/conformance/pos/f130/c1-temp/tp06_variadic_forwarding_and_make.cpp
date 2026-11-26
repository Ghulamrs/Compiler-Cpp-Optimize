#include <cstdio>
#include <utility>
#include <string>
struct P { std::string s; int n; P(std::string s, int n) : s(std::move(s)), n(n) {} P(const char *c) : s(c), n(-1) {} };
template <class T, class... Args> T make(Args &&... args) { return T(std::forward<Args>(args)...); } template <class T, class... Args> T *make_ptr(Args &&... args) { return new T(std::forward<Args>(args)...); }
int main() { P a = make<P>("x", 1); P b = make<P>(std::string("yy"), 2); P *c = make_ptr<P>("z"); std::printf("%s %d %s %d %s %d\n", a.s.c_str(), a.n, b.s.c_str(), b.n, c->s.c_str(), c->n); delete c; return 0; }
