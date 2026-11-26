#include <cstdio>
int y = 1;
int z[1] = { 5 };
int f(int v) { return v + 3; }
int t(int v) { return v * 100; }
int m(int a, int b) { return a * 10 + b; }
#define x 3
#define f(a) f(x * (a))
#undef x
#define x 2
#define g f
#define z z[0]
#define h g(~
#define m(a) a(w)
#define w 0,1
#define t(a) a
#define p() int
#define q(x) x
#define r(x,y) x ## y
#define str(x) # x
int r1 = f(y+1) + f(f(z)) % t(t(g)(0) + t)(1);
int r2 = g(x+(3,4)-w) | h 5) & m
(f)^m(m);
p() i[q()] = { q(1), r(2,3), r(4,), r(,5), r(,) };
char c[2][6] = { str(hello), str() };
int main() {
    std::printf("%d %d %d %d %d %d [%s] [%s]\n", r1, r2, (int)(sizeof i / sizeof i[0]), i[0], i[1],
                i[3], c[0], c[1]);
    return 0;
}
