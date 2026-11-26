struct S { typedef int T; };
int main() { typename S::T x = 0; return x; }
