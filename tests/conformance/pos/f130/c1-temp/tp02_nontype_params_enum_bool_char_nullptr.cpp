#include <cstdio>
#include <cstddef>
enum E { e1 = 1, e2 }; enum class SC : char { a = 'a' };
template <E V> int ev() { return V; } template <bool B> int bv() { return B ? 1 : 0; } template <char C> int cv() { return C; } template <SC V> int sv() { return (int)V; } template <std::nullptr_t N> int nv() { return 1; } template <unsigned long long V> int big() { return (int)(V >> 40); } template <long L> int neg() { return (int)L; } template <int N = 3> int def() { return N; }
int main() { std::printf("%d %d %d %d %d %d %d %d\n", ev<e2>(), bv<true>(), cv<'x'>(), sv<SC::a>(), nv<nullptr>(), big<1ull << 42>(), neg<-7>(), def<>()); return 0; }
