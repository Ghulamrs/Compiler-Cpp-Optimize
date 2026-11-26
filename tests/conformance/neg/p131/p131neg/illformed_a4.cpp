struct S { explicit operator int() const { return 5; } }; int main() { S s; int r = s + 1; return r; }
