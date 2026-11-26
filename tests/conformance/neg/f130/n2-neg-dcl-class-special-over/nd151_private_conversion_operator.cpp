class S { operator int() const; }; int main() { S s; int i = s; return i; }
