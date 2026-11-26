struct S { int &r; }; int main() { int i; S a{i}, b{i}; a = b; return 0; }
