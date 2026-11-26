int main() { int x = 1; struct L { int f() { return x; } }; return L().f(); }
