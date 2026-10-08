// [class.virtual]/4: a function marked `override` must override a virtual of
// a base. `g(int)` overrides nothing - B's is `g()` - so clang refuses it at
// the name: "only virtual member functions can be marked 'override'".
struct B { virtual int g() { return 0; } virtual ~B() {} };
struct D : B { int g(int k) override { return k; } };
int main() { D d; return d.g(1); }
