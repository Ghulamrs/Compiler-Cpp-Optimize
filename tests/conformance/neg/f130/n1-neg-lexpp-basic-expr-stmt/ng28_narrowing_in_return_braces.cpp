struct S { char c; }; S f(int v) { return {v}; } int main() { return f(1).c; }
