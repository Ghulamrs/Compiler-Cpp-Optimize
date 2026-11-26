// Overloading between member templates and non-templates, constructors templates vs copy ctor (template never a copy ctor), conversion ctor templates.
#include <cstdio>
struct S {
    S() : kind("def") {}
    S(const S &) : kind("copy") {}
    template <class T> S(const T &) : kind("tmpl") {}
    template <class T> const char *m(T) { return "m<T>"; }
    const char *m(int) { return "m(int)"; }
    const char *m(double) { return "m(double)"; }
    const char *kind;
};
int main() {
    S a; S b = a; S c = 1; S d = 2.5; const S e = a; S f = e;
    std::printf("%s %s %s %s %s\n", a.kind, b.kind, c.kind, d.kind, f.kind);
    std::printf("%s %s %s %s\n", a.m(1), a.m(1.5), a.m('c'), a.m(1u));
    std::printf("%s\n", a.m<int>(1));
    return 0;
}
