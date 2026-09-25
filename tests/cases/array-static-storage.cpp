// **Arrays of a class with static storage duration**: at file scope, in a
// namespace, static in a function, and as a member - every element built
// before main (or on first passage, under the guard) and destroyed last
// first at exit, in reverse order of construction across the objects.
extern "C" int printf(const char *, ...);
static int built = 0, gone = 0;
struct T {
    int id;
    T() : id(built++) {}
    ~T() { gone++; printf("~%d ", id); }
};
struct One { int v; One() : v(42) {} };
T table[3];
static One ones[2];
namespace N { T inner[2]; }
struct Holder { T parts[2]; int tail; Holder() : tail(7) {} };
Holder h;
int main() {
    printf("%d %d %d %d %d\n", built, table[2].id, ones[1].v, N::inner[1].id, h.parts[1].id);
    printf("%d %d\n", gone, h.tail);
    return 0;
}
