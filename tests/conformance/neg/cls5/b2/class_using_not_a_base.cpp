struct A { int f() { return 1; } };
struct D { using A::f; };
int main() { D d; return 0; }
