// [class.abstract]: abstract through a second base, on every target.
struct One { virtual void a() {} };
struct Two { virtual void b() = 0; };
struct D : One, Two { void c() {} };
int main() { D d; d.c(); return 0; }
