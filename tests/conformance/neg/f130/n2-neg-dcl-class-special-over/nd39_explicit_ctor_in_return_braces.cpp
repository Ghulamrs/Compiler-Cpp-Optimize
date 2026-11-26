struct S { explicit S(int) {} }; S f() { return {1}; } int main() { f(); return 0; }
