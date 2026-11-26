// [temp.mem]/1: a member class template of a class template, as an allocator's rebind, named with and without 'template'.
#include <cstdio>
namespace lib {
template <class T> struct alloc {
    typedef T value_type;
    template <class U> struct rebind { typedef alloc<U> other; };
    int size() const { return (int)sizeof(T); }
};
}
template <class A> int otherSize() { typename A::template rebind<double>::other o; return o.size(); }
int main() {
    lib::alloc<char>::rebind<long long>::other a;
    std::printf("%d %d\n", a.size(), otherSize<lib::alloc<int> >());
    return 0;
}
