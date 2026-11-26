namespace N { int f() { return 3; } }
int main() { using N::f; return f() == 3 ? 0 : 1; }
