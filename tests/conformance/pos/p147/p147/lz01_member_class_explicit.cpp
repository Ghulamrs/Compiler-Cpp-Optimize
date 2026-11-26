// [temp.expl.spec]/1 and [temp.inst]/1: a member class of one specialization replaced, the primary's used for another.
#include <cstdio>
template <class T> struct S {
    struct In { T v; int get() const { return (int)sizeof(T) + 10; } static int id(); };
    In make() const { In i; i.v = T(); return i; }
};
template <class T> int S<T>::In::id() { return 5; }
template <> struct S<int>::In { long w; int get() const { return 99; } static int id() { return 6; } };
int main() {
    S<int>::In a; a.w = 3;
    S<char> c; S<char>::In b = c.make();
    std::printf("%d %d %d %d %d\n", a.get(), b.get(), S<int>::In::id(), S<char>::In::id(), (int)sizeof(S<int>::In) / (int)sizeof(long));
    return 0;
}
