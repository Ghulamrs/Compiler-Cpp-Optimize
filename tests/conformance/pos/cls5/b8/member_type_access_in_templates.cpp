// A class template's private typedef named from its members, a member defined outside it, a nested class,
// and a friend function template; a protected typedef named from a derived class template.
#include <cstdio>

template <class T> class Box {
    typedef T value_type;
    T v;
    template <class U> friend U unbox(const Box<U> &);
public:
    explicit Box(T x) : v(x) {}
    value_type get() const;
    struct Peek {
        typename Box::value_type of(const Box &b) const { return b.v; }
    };
};

template <class T> typename Box<T>::value_type Box<T>::get() const {
    typename Box<T>::value_type r = v;
    return r;
}

template <class U> U unbox(const Box<U> &b) {
    typename Box<U>::value_type r = b.v;
    return r;
}

struct Base {
protected:
    typedef int Count;
};

template <class T> struct Derived : Base {
    Count n() const {
        Base::Count c = sizeof(T);
        return c;
    }
};

int main() {
    Box<int> b(4);
    Box<int>::Peek p;
    Derived<double> d;
    std::printf("%d %d %d %d\n", b.get(), unbox(b), p.of(b), d.n());
    return 0;
}
