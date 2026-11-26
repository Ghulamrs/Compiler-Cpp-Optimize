struct B { bool v; };
bool operator&&(B a, B b) { return a.v && b.v; }
bool operator||(B a, B b) { return a.v || b.v; }
int operator,(B, int i) { return i; }
int main() { B t = {true}, f = {false}; return (t && t) && !(f || f) && ((t, 3) == 3) ? 0 : 1; }
