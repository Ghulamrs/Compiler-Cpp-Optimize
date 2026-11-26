// 'template' names a member template, and 'Plain' is a class.
struct S { struct Plain { int v; }; };
int main() { S::template Plain<int> p; return 0; }
