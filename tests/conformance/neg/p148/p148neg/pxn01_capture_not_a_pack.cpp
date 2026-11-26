// [expr.prim.lambda]/23: '...' after a capture expands a pack, and x is none.
template <class... Ts> int f(int x, Ts... ts) { auto l = [x...] { return 0; }; return l() + (int)sizeof...(ts); }
int main() { return f(1, 2); }
