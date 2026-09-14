// **An array of a class with a destructor and no constructor is destroyed
// too** - local, file-scope, through a base, and `delete[]` of a class
// whose destructor is virtual, which takes the static type and the cookie's
// count as [expr.delete] says. The constructor-less arrays were laid out as
// bytes and never destroyed until 2026-09-14 (findings 3 and 7).
extern "C" int printf(const char *, ...);
struct Bd { int v; Bd() : v(0) {} ~Bd() { printf("~Bd %d\n", v); } };
struct Dd : Bd { };
struct Only { int v; ~Only() { printf("~Only %d\n", v); } };
struct Dd2 : Only { };
struct Vd { int v; Vd() : v(0) {} virtual ~Vd() { printf("~Vd %d\n", v); } };
struct H { Only arr[2]; ~H() { printf("~H\n"); } };
Only gonly[2];
static Dd2 gdd2[1];
int main() {
    gonly[0].v = 1; gonly[1].v = 2; gdd2[0].v = 9;
    { Only o[2]; o[0].v = 5; o[1].v = 6; }
    printf("only-scope done\n");
    { Dd2 d[2]; d[0].v = 15; d[1].v = 16; }
    printf("dd2-scope done\n");
    { Dd *d = new Dd[2]; d[0].v = 7; d[1].v = 8; delete[] d; }
    printf("Dd done\n");
    { Vd *v = new Vd[2]; v[0].v = 40; v[1].v = 41; delete[] v; }
    printf("Vd done\n");
    { H h; h.arr[0].v = 50; h.arr[1].v = 51; }
    printf("member done\n");
    { static Only s[2]; s[0].v = 60; s[1].v = 61; }
    printf("end\n");
    return 0;
}
