// [basic.lookup.argdep] with [over.match.best]: argument-dependent lookup finds who in A and in B, equally good.
namespace A { struct X {}; }
namespace B { struct Y {}; int who(A::X, Y) { return 2; } }
namespace A { int who(X, B::Y) { return 1; } }
int main() { A::X x; B::Y y; return who(x, y); }
