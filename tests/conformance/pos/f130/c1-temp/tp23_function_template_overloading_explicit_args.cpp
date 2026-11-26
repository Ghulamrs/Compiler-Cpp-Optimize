#include <cstdio>
template <class T> int f(T) { return 1; } template <class T, class U> int f(T, U) { return 2; } template <class T> int g() { return (int)sizeof(T); } template <class R, class A> R h(A a) { return (R)a; } template <class... Ts> int cnt() { return sizeof...(Ts); } template <class T, class... Ts> int firstsz(Ts...) { return (int)sizeof(T) * 10 + sizeof...(Ts); }
int main() { std::printf("%d %d %d %d %g %d %d %d\n", f(1), f(1, 2), f<double>(1), g<long long>(), h<double>(3), cnt<>(), cnt<int, int>(), firstsz<char>(1, 2.0)); return 0; }
