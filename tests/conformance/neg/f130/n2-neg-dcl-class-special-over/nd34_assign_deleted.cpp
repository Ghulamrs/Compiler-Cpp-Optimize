struct S { S &operator=(const S &) = delete; }; int main() { S a, b; a = b; return 0; }
