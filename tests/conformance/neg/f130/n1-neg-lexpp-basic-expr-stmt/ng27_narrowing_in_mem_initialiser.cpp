struct S { short s; S(int v) : s{v} {} }; int main() { S s(1); return s.s; }
