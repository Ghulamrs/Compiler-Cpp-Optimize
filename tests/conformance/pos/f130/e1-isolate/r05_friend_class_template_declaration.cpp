#include <cstdio>
template <class T> class H { int secret = 7; template <class U> friend class H; public: template <class U> int peek(const H<U> &o) const { return o.secret; } };
int main() { H<int> a; H<char> b; std::printf("%d\n", a.peek(b)); return 0; }
