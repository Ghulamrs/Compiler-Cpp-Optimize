struct B { virtual int f() = delete; };
int main() { B b; B *p = &b; return p->f(); }
