#include <cstdio>
template <class T> struct S { T v; T get() const { return v; } }; template struct S<int>; template <class T> T twice(T x) { return x * 2; } template int twice<int>(int); template double twice(double); extern template struct S<long>; template class S<char>;
int main() { S<int> s{2}; S<long> l{3}; std::printf("%d %d %g %ld\n", s.get(), twice(4), twice(1.5), l.get()); return 0; }
