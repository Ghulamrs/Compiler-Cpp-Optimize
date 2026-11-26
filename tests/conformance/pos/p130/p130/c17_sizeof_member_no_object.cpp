struct S { int m; double d; };
int main() { return sizeof(S::m) == sizeof(int) && sizeof(S::d) == 8 ? 0 : 1; }
