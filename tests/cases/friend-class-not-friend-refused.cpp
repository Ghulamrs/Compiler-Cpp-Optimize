// [class.friend]/10: friendship is neither inherited nor transitive - a class derived from the
// friend is not one. clang: "'secret' is a private member of 'Box'" (6:59).
class Box { friend class Peeker; int secret = 3; };
class Peeker { public: int look(const Box &b) const { return b.secret; } };
class Child : public Peeker {
public: int poke(const Box &b) const { return b.secret; } };
int main() { Box b; Child c; return c.poke(b) - 3; }
