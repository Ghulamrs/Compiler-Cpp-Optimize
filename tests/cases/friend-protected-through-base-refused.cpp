// The other side of [class.protected]: a friend of D may not name B's protected
// member through a B - cl6x 8.2.2 says error #412-D, clang refuses it too.
struct B { protected: int v; };
struct D : B { friend int get(const B &b) { return b.v; } };
int main() { return 0; }
