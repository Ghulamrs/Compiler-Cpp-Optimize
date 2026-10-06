struct R { R(int); int v; };
R::R(int v) : v(v) {}
int main() { R r[] = {}; return 0; }
