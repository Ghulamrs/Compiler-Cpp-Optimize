struct S { explicit operator int() const { return 1; } }; int main() { S s; int i = s; return i; }
