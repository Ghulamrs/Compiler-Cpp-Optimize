struct I1 { virtual int a() { return 1; } virtual ~I1() {} };
struct I2 { virtual int b() { return 2; } virtual ~I2() {} };
struct X : I1, I2 { int a() { return 10; } int b() { return 20; } };
int main() { X x; I2 *p = &x; I1 *q = &x; return p->b() + q->a() == 30 ? 0 : 1; }
