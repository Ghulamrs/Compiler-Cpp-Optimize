struct S { static int x; };
int S::x = 1;
int main() { using S::x; return x; }
