// A 16-byte and a 24-byte struct returned through a function pointer and a virtual function (hidden return pointer handling).
#include <cstdio>
struct S16 { long long a, b; }; struct S24 { long long a, b, c; };
S16 make16(int k) { S16 s = {k, k * 2}; return s; }
S24 make24(int k) { S24 s = {k, k * 2, k * 3}; return s; }
struct I { virtual S16 v16(int) = 0; virtual S24 v24(int) = 0; virtual ~I() {} };
struct Impl : I { S16 v16(int k) override { return make16(k + 1); } S24 v24(int k) override { return make24(k + 1); } };
int main() { S16 (*f16)(int) = make16; S24 (*f24)(int) = make24; Impl im; I &i = im; S16 a = f16(1), b = i.v16(1); S24 c = f24(2), d = i.v24(2); std::printf("%lld %lld %lld %lld %lld %lld\n", a.b, b.b, c.c, d.c, a.a + b.a, c.a + d.a); return 0; }
