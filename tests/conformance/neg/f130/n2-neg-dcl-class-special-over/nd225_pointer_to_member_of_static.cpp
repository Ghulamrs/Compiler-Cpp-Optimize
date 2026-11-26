struct S { static int s; }; int main() { int S::*p = &S::s; return 0; }
