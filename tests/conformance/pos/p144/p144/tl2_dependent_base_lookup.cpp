// [temp.dep]/3: a base that depends on a template parameter is not searched by unqualified lookup -
// `this->m` and `B<T>::m` reach its members, and a global of the name is what the bare name means.
#include <cstdio>
int count = 40;
template <class T> struct B { int m; int count; int get() const { return m; } B() : m(1), count(2) {} };
template <class T> struct D : B<T> {
    int f() const { return this->m + B<T>::m + this->get(); }
    int g() const { return count; }
};
struct NotTemplate : B<int> { int h() const { return m + count; } };
int main() {
    D<int> d;
    NotTemplate n;
    std::printf("%d %d %d\n", d.f(), d.g(), n.h());
    return 0;
}
