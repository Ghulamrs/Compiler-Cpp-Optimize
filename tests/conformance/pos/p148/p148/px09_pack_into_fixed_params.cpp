// [temp.arg]/9 and CWG 1430's C++11 wording: a pack expansion as the arguments of a template with fixed parameters.
#include <cstdio>
template <class A, class B> struct Two { static const int v = (int)sizeof(A) * 10 + (int)sizeof(B); };
template <class A, class B, class C = char> struct Three { static const int v = (int)sizeof(A) + (int)sizeof(B) + (int)sizeof(C); };
template <class... Ts> struct Use { static const int two = Two<Ts...>::v; };
template <class... Ts> struct UseThree { static const int v = Three<Ts...>::v; };
template <class T, class... Ts> struct Lead { static const int v = Two<T, Ts...>::v; };
int main() {
    std::printf("%d %d %d %d\n", Use<int, char>::two, UseThree<int, int>::v, UseThree<int, int, double>::v, Lead<char, short>::v);
    return 0;
}
