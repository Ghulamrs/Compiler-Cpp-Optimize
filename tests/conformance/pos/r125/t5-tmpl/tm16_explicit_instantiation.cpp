// [temp.explicit]: explicit instantiation definitions of a function template, a class template and a member; extern template declarations.
#include <cstdio>
template <class T> T twice(T t) { return t + t; }
template <class T> struct Box { T v; T get() const { return v; } };
template int twice<int>(int);
template double twice(double);
template struct Box<int>;
template char Box<char>::get() const;
extern template struct Box<long>;
template struct Box<long>;
int main() {
    Box<int> b = {4}; Box<char> c = {'c'}; Box<long> l = {5L};
    std::printf("%d %g %d %c %ld\n", twice(1), twice(1.5), b.get(), c.get(), l.get());
    return 0;
}