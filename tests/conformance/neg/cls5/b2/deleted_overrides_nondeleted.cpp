struct B { virtual int f() { return 1; } };
struct D : B { int f() = delete; };
int main() { D d; return 0; }
