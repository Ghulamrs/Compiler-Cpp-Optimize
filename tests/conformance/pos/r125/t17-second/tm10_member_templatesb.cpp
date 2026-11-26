// Member function templates, constructor templates, member templates of class templates (out of line), explicit specialisation of a member, static members of class templates, template keyword for dependent member templates.
#include <cstdio>
template <class T> struct Box {
    T v;
    Box(T v) : v(v) {}
    template <class U> Box(const Box<U> &o) : v((T)o.v) {}
    template <class U> U as() const { return (U)v; }
    template <class U> static U twice(U u) { return u + u; }
    static int count;
    template <int N> int scaled() const { return (int)v * N; }
};
template <class T> int Box<T>::count = 0;
template <class T> struct Out { template <class U> U conv(U) const; };
template <class T> template <class U> U Out<T>::conv(U u) const { return u * 2; }
int useScaled(const Box<int> &t) { return t.scaled<3>(); }
template <class T> struct Sp { const char *who() const { return "generic"; } };
template <> const char *Sp<int>::who() const { return "int member spec"; }
int main() {
    Box<int> a(3); Box<double> b = a; Box<int> c(Box<double>(2.7));
    std::printf("%d %g %d\n", a.v, b.v, c.v);
    std::printf("%g %d %d\n", a.as<double>(), b.as<int>(), Box<int>::twice(4));
    Box<int>::count = 5; Box<double>::count = 6; std::printf("%d %d\n", Box<int>::count, Box<double>::count);
    std::printf("%d %d\n", a.scaled<2>(), useScaled(a));
    Out<char> o; std::printf("%d %g\n", o.conv(5), o.conv(1.5));
    std::printf("%s %s\n", Sp<int>().who(), Sp<char>().who());
    return 0;
}
