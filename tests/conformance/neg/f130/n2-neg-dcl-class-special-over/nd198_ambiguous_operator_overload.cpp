struct S { operator int() const; operator double() const; }; int main() { S s; return s + 1; }
