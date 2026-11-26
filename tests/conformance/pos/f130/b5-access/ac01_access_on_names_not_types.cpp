#include <cstdio>
class S { typedef int Hidden; Hidden v = 3; public: Hidden get() const { return v; } static S make() { return S(); } }; template <class T> T id(T t) { return t; }
int main() { auto s = S::make(); auto h = s.get(); decltype(s.get()) k = 4; std::printf("%d %d %d\n", h, k, id(s.get())); return 0; }
