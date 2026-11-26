#include <cstdio>
struct V { virtual ~V() = 0; }; V::~V() { std::printf("~V "); }
struct J : V { };
struct K { virtual ~K() = 0; virtual int f() { return 1; } }; K::~K() {}
struct M : K { int f() { return 2; } };
int use(int (*g)(int), int x); int use(int g(int), int x) { return g(x) + 1; }
int inc(int x) { return x + 1; }
char gs[] = {"xyz"}; wchar_t ws[] = {L"ab"};
void (*fp)(int) throw(int) = 0; typedef int (*Op)(int);
int main() { { J j; } { M m; K &k = m; std::printf("%d ", k.f()); } Op o = inc; std::printf("%d %d %s %d %d\n", use(inc, 1), o(4), gs, (int)sizeof(gs), (int)sizeof(ws) / (int)sizeof(wchar_t)); return 0; }
