// [expr.prim.general]/1 and [dcl.init.ref]/5: a string literal is an lvalue array, which a reference to an array binds
// where it lies; and [temp.deduct.call]/3: `const T &` from a `const char[3]` deduces T as `char[3]`, as from a
// `char[3]`, so the two calls name one specialization.
#include <cstdio>
#include <string>
#include <utility>
void p(const char *c) { std::printf("<%s> ", c); }
void g(const std::string &s) { std::printf("[%s] ", s.c_str()); }
void h(const char (&a)[3]) { p(a); }
template <class T> void ref(const T &t) { p(t); }
template <class T> void fwd(T &&t) { p(t); }
template <class T> void one(T &&t) { g(std::forward<T>(t)); }
template <class... A> void many(A &&... a) { g(std::forward<A>(a)...); }
int main() { h("cd"); char arr[] = "xy"; ref(arr); ref("uv"); const char carr[] = "zw"; ref(carr); fwd(arr); fwd("st"); one("ab"); many("gh"); std::printf("%d\n", (int)sizeof(arr)); return 0; }
