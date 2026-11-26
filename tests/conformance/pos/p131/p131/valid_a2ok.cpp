struct S { int c; int &r; S(int &x) : c(1), r(x) {} S &operator=(const S &o) { c = o.c; return *this; } }; int main() { int x = 0; S a(x), b(x); a = b; return a.c; }
