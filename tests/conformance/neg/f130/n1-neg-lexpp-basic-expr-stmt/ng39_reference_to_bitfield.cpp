struct S { int b : 3; }; int main() { S s; int &r = s.b; return r; }
