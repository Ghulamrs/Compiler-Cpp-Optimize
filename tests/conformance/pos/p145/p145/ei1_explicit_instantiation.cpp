// [temp.explicit]: explicit instantiation definitions of a class template, of one member of another
// specialization, of a function template with its arguments written and deduced; an extern one before.
#include <cstdio>
template <class T> T twice(T t) { return t + t; }
template <class T> struct Box { T v; T get() const { return v; } T neg() const { return -v; } };
template <class T> struct Pair { T a, b; T sum() const { return a + b; } };
template int twice<int>(int);
template double twice(double);
template struct Box<int>;
template char Box<char>::get() const;
extern template struct Pair<long>;
template struct Pair<long>;
template class Pair<short>;
int main() {
    Box<int> b = {4};
    Box<char> c = {'c'};
    Pair<long> l = {5L, 6L};
    Pair<short> s = {1, 2};
    std::printf("%d %g %d %d %c %ld %d\n", twice(1), twice(1.5), b.get(), b.neg(), c.get(), l.sum(), (int)s.sum());
    return 0;
}
