// [temp.alias] inside a class template: the alias sees the class's own parameter.
#include <cstdio>
template <class T> struct Box {
    template <class U> using With = Box<U>;
    template <class U> using From = U (*)(T);
    T v;
};
long twice(int x) { return 2L * x; }
template <class B> int sz() { return (int)sizeof(typename B::template With<double>); }
int main() {
    Box<int>::With<char> c; c.v = 'q';
    Box<int>::From<long> f = twice;
    std::printf("%c %ld %d\n", c.v, f(21), sz<Box<int> >());
    return 0;
}
