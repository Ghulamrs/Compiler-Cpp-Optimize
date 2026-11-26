struct S { int b : 3; }; int main() { S s; return (int)sizeof(s.b); }
