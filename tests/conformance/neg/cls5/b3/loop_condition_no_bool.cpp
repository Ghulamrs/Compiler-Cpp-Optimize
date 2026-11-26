struct C { int v; C(int x) : v(x) {} };
int main() { int n = 2; while (C c = C(n--)) {} return 0; }
