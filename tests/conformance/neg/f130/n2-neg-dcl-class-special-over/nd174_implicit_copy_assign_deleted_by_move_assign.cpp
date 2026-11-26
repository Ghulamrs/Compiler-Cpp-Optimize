struct S { S &operator=(S &&) { return *this; } }; int main() { S a, b; a = b; return 0; }
