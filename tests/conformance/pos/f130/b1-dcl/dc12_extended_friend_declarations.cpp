#include <cstdio>
class K; template <class T> class H { int secret = 7; friend T; friend class K; template <class U> friend class H; public: template <class U> int peek(const H<U> &o) const { return o.secret; } };
struct Spy { int look(const H<Spy> &h) { return h.secret; } }; class K { public: int look(const H<int> &h) { return h.secret; } };
int main() { H<Spy> h; H<int> hi; Spy s; K k; std::printf("%d %d %d\n", s.look(h), k.look(hi), h.peek(hi)); return 0; }
