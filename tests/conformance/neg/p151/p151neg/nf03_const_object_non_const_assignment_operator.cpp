// [over.match.funcs]/4: a const object binds only a const member's implicit object parameter - its operator= too.
struct S { int v; S &operator=(int x) { v = x; return *this; } };
int main() { const S s = {1}; s = 5; return 0; }
