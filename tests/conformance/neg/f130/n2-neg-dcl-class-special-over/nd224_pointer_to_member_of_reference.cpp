struct S { int &r; }; int main() { int &S::*p = &S::r; return 0; }
