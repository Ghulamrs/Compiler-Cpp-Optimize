template <class T> struct has_type {
    typedef char yes; typedef long no;
    template <class U> static yes test(typename U::type *);
    template <class U> static no test(...);
    static const bool value = sizeof(test<T>(0)) == sizeof(yes);
};
struct A { typedef int type; };
int main() { return has_type<A>::value && !has_type<int>::value ? 0 : 1; }
