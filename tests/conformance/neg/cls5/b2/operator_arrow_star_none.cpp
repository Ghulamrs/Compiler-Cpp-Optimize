struct S { int m; };
struct H { S *p; };
int main() { S s = {1}; H h = {&s}; return h->*&S::m; }
