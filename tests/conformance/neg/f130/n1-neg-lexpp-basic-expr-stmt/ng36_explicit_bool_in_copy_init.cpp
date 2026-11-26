struct S { explicit operator bool() const { return true; } }; int main() { S s; bool b = s; return b; }
