struct S { explicit operator int() const { return 1; } }; int main() { S s; return s + 1; }
