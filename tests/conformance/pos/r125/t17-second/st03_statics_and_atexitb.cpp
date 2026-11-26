// [stmt.dcl]/4 static locals initialised once (also when the function recurses), [basic.start.term]: static locals destroyed in reverse order of construction completion, after main returns; exit() runs them too; atexit ordering.
#include <cstdio>
#include <cstdlib>
struct D { const char *n; D(const char *n) : n(n) { std::printf("+%s ", n); } ~D() { std::printf("-%s ", n); } };
D g("g");
int rec(int n) { static int init = (std::printf("init "), n); if (n > 0) rec(n - 1); return init; }
void a() { static D sa("sa"); }
void b() { static D sb("sb"); }
void onExit() { std::printf("atexit "); }
int main() {
    std::printf("%d ", rec(3));
    b(); a(); b();
    std::printf("| ");
    return 0;
}
