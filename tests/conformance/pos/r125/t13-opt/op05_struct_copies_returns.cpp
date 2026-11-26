// Struct passing/returning of sizes 1, 2, 3, 4, 8, 12, 16, 24 bytes by value and through functions, nested struct copies, arrays of structs copied in loops, struct with float members returned.
#include <cstdio>
struct S1 { char a; }; struct S2 { short a; }; struct S3 { char a, b, c; }; struct S4 { int a; }; struct S8 { int a, b; }; struct S12 { int a, b, c; }; struct S16 { long long a, b; }; struct S24 { long long a, b, c; };
struct F8 { float a, b; }; struct D16 { double a, b; }; struct Mix { int i; double d; char c; };
S1 f1(S1 s) { s.a++; return s; } S2 f2(S2 s) { s.a++; return s; } S3 f3(S3 s) { s.c++; return s; } S4 f4(S4 s) { s.a++; return s; }
S8 f8(S8 s) { s.b++; return s; } S12 f12(S12 s) { s.c++; return s; } S16 f16(S16 s) { s.b++; return s; } S24 f24(S24 s) { s.c++; return s; }
F8 ff8(F8 s) { s.b += 1; return s; } D16 fd16(D16 s) { s.b += 1; return s; } Mix fmix(Mix m) { m.d += 1; m.c++; return m; }
struct Nest { S8 in; S3 tri; int tail; };
Nest nest(Nest n) { n.in.a += n.tail; n.tri.b++; return n; }
int main() {
    S1 a = {1}; S2 b = {2}; S3 c = {1, 2, 3}; S4 d = {4}; S8 e = {5, 6}; S12 f = {7, 8, 9}; S16 g = {10, 11}; S24 h = {12, 13, 14};
    a = f1(a); b = f2(b); c = f3(c); d = f4(d); e = f8(e); f = f12(f); g = f16(g); h = f24(h);
    std::printf("%d %d %d %d %d %d %lld %lld\n", a.a, b.a, c.c, d.a, e.b, f.c, g.b, h.c);
    F8 p = {1.5f, 2.5f}; D16 q = {1.25, 2.25}; Mix m = {1, 2.5, 'a'};
    p = ff8(p); q = fd16(q); m = fmix(m);
    std::printf("%g %g %g %g %d %g %c\n", p.a, p.b, q.a, q.b, m.i, m.d, m.c);
    Nest n = {{1, 2}, {3, 4, 5}, 6}; n = nest(n); std::printf("%d %d %d\n", n.in.a, n.tri.b, n.tail);
    S12 arr[4] = {{1, 1, 1}, {2, 2, 2}, {3, 3, 3}, {4, 4, 4}}; S12 copy[4]; for (int i = 0; i < 4; ++i) copy[3 - i] = arr[i]; std::printf("%d %d\n", copy[0].a, copy[3].c);
    S24 big[2]; big[0] = h; big[1] = big[0]; big[1].a++; std::printf("%lld %lld\n", big[0].a, big[1].a);
    return 0;
}
