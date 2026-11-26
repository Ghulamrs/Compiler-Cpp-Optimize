#include <cstdio>
template <class T> class H { int secret = 7; friend T; }; struct Spy { int look(const H<Spy> &h) { return h.secret; } };
int main() { H<Spy> h; Spy s; std::printf("%d\n", s.look(h)); return 0; }
