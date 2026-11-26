struct S { int m; };
struct H { S *p; int &operator->*(int S::*pm) { return p->*pm; } };
int main() { S s = {4}; H h = {&s}; return (h->*&S::m) == 4 ? 0 : 1; }
