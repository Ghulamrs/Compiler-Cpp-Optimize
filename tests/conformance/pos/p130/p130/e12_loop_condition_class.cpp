struct C { int v; C(int x) : v(x) {} explicit operator bool() const { return v > 0; } };
int main() { int n = 3, k = 0; while (C c = C(n--)) k += c.v; return k == 6 ? 0 : 1; }
