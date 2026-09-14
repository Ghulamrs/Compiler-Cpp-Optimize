// A trivially copyable class of 8 bytes or less comes back in registers
// (A5:A4 on the C6000); one with a destructor goes through the hidden pointer
// whatever its size, and a member function's result is no different. Results
// nobody reads are still built, and destroyed.
extern "C" int printf(const char *, ...);
struct Pt { int x, y; };
struct Tiny { unsigned char a, b; };
struct Three { char a, b, c; };
struct Dtor { int v; ~Dtor() { printf("~%d ", v); } };
struct Box { Pt at; Pt size() const { Pt p = {at.y, at.x}; return p; } };
static Pt mk(int x, int y) { Pt p = {x, y}; return p; }
static Tiny tiny(int a) { Tiny t = {(unsigned char)a, (unsigned char)(a * 2)}; return t; }
static Three three(char c) { Three t = {c, (char)(c + 1), (char)(c + 2)}; return t; }
static Dtor dt(int v) { Dtor d = {v}; return d; }
static Pt (*fp)(int, int) = mk;
int main() {
    char odd = 'o'; Three t3 = three('a');
    Pt p = mk(3, 4); Tiny t = tiny(7); Box b = {{1, 2}};
    printf("%d %d %d %d %d %d %d\n", p.x, p.y, t.a, t.b, b.size().x, b.size().y, fp(8, 9).y);
    printf("%c%c%c %c\n", t3.a, t3.b, t3.c, odd);
    printf("%d ", dt(5).v);
    mk(1, 1); dt(6);
    printf("\n");
    return mk(0, 2).y;
}
