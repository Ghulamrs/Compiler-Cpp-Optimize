struct B { virtual int f() = delete; virtual int g() { return 1; } virtual ~B() {} };
int main() { B b; return b.g() == 1 ? 0 : 1; }
