struct S { int a; virtual void f() {} };
int main() { S s = {1}; return s.a; }
