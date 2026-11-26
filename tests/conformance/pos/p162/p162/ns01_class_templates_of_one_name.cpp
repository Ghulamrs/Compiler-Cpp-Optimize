// [basic.scope.namespace]: one name, one template per namespace - each with its own members, out-of-line definitions,
// statics, partial and explicit specializations, found by the scope a name is read in.
#include <cstdio>
namespace a {
    template <class T> struct X { int f() const; static int s; };
    template <class T> int X<T>::f() const { return 1; }
    template <class T> int X<T>::s = 10;
    template <> struct X<char> { int f() const { return 11; } };
    template <class T> struct X<T *> { int f() const { return 12; } };
    int inside() { return X<int>().f(); }
}
namespace b {
    template <class T> struct X { int f() const; static int s; };
    template <class T> int X<T>::f() const { return 2; }
    template <class T> int X<T>::s = 20;
    template <> struct X<char> { int f() const { return 21; } };
    template <class T> struct X<T *> { int f() const { return 22; } };
    int inside() { return X<int>().f(); }
    namespace c { int deeper() { return X<int>().f(); } }
}
template <class T> struct X { int f() const { return 3; } };
namespace d { typedef a::X<int> AX; typedef ::X<int> GX; int global() { return ::X<long>().f(); } }
int main() {
    a::X<int> p;
    b::X<int> q;
    X<int> r;
    ::X<char> s;
    std::printf("%d %d %d %d\n", p.f(), q.f(), r.f(), s.f());
    std::printf("%d %d %d %d\n", a::X<char>().f(), b::X<char>().f(), a::X<int *>().f(), b::X<int *>().f());
    std::printf("%d %d %d\n", a::X<int>::s, b::X<int>::s, a::inside() + b::inside() + b::c::deeper());
    std::printf("%d %d %d\n", d::AX().f(), d::GX().f(), d::global());
    return 0;
}
