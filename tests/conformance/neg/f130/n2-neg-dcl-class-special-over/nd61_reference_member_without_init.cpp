struct S { int &r; }; int main() { S s; return s.r; }
