#include <cstdio>
#include <memory>
template <class T> struct Odd { typedef T value_type; template <class U> struct rebind { typedef Odd<long> other; }; };
template <class T, class... More> struct Many { typedef T value_type; template <class U> struct rebind { typedef Many<double> other; }; };
template <class A, class T> char t1(typename A::template rebind<T>::other *) { return 0; }
template <class A, class T> long t1(...) { return 0; }
int main() {
    std::printf("%d %d %d %d\n", (int)sizeof(t1<Odd<int>, char>(0)), (int)sizeof(t1<Many<int>, char>(0)),
                (int)sizeof(std::allocator_traits<Odd<int> >::rebind_alloc<char>::value_type),
                (int)sizeof(std::allocator_traits<Many<int> >::rebind_alloc<char>::value_type));
}
