struct S { explicit operator bool() const { return true; } operator int() const { return 5; } }; int main() { S s; if (s) return s + 1; return 0; }
