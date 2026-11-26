#include <cstdio>
template <class T> struct CI { const T *p; CI(const T *q) : p(q) {} };
template <class T> struct It { T *p; It(T *q) : p(q) {} operator CI<T>() const { return CI<T>(p); } };
template <class T> struct L {
    typedef It<T> iterator;
    typedef CI<T> const_iterator;
    T v[4];
    iterator begin() { return iterator(v); }
    template <class... A> int emplace(const_iterator pos, A &&... a) { return (int)(pos.p - v) + (int)sizeof...(A); }
    template <class U> int one(const_iterator pos, U u) { return (int)(pos.p - v) + (int)u; }
};
int main() {
    L<int> l;
    std::printf("%d %d\n", l.emplace(l.begin(), 1, 2), l.one(l.begin(), 5));
}
