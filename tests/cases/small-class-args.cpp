// A trivially copyable class of 8 bytes or less goes to a function by value
// in registers (A5:A4 on the C6000), like a scalar of its size; one with a
// destructor or a copy constructor goes by the address of a copy whatever its
// size, and the two shapes may sit side by side in one argument list. The
// copy's destruction is counted, not printed in order: Itanium destroys it in
// the caller after the call, the Microsoft ABI in the callee before printf.
extern "C" int printf(const char *, ...);
static int gone;
struct Pt { int x, y; };
struct Tiny { unsigned char a, b; };
struct Three { char a, b, c; };
struct Dtor { int v; ~Dtor() { ++gone; } };
struct Cpy { int v; Cpy(int i) : v(i) {} Cpy(const Cpy &o) : v(o.v + 100) {} };
static int sum(Pt p, Tiny t, Three r) { return p.x + p.y + t.a + t.b + r.a + r.b + r.c; }
static int mixed(int a, Pt p, double d, Three r, Dtor k, Cpy c, Pt q) { return a + p.y + (int)d + r.c + k.v + c.v + q.x; }
static int (*fp)(Pt, Tiny, Three) = sum;
struct Box { Pt at; int shift(Pt by) const { return at.x + by.x + at.y + by.y; } };
int main() {
    Pt p = {1, 2}, q = {30, 40}; Tiny t = {3, 4}; Three r = {5, 6, 7}; Box b = {{100, 200}};
    printf("%d %d %d\n", sum(p, t, r), fp(q, t, r), b.shift(q));
    { Dtor k = {9}; Cpy c(8); printf("%d\n", mixed(1, p, 2.5, r, k, c, q)); }
    printf("%d\n", gone);
    return sum(q, t, r) % 100;
}
