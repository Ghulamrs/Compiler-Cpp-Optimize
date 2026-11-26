struct B { ~B() {} };
struct D : B { ~D() override {} };
int main() { return 0; }
