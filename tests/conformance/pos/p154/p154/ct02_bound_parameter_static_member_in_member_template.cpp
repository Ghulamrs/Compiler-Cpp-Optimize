#include <type_traits>
template <class T> struct R : std::integral_constant<int, 1> {};
template <> struct R<double> : std::integral_constant<int, 2> {};
template <class T> struct C {
    template <class X, typename std::enable_if<(R<X>::value <= R<T>::value), int>::type = 0> C(X) {}
};
int main() { C<double> c(1.0f); return 0; }
