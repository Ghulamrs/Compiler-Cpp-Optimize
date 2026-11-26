#include <cstdio>
typedef char yes[1]; typedef char no[2];
template <class T> struct has_foo { template <class U> static yes &test(decltype(&U::foo)); template <class U> static no &test(...); static const bool value = sizeof(test<T>(0)) == sizeof(yes); };
struct A { void foo(); }; struct B {};
template <class T> struct is_class { template <class U> static yes &test(int U::*); template <class U> static no &test(...); static const bool value = sizeof(test<T>(0)) == 1; };
int main() { std::printf("%d %d %d %d\n", (int)has_foo<A>::value, (int)has_foo<B>::value, (int)is_class<A>::value, (int)is_class<int>::value); return 0; }
